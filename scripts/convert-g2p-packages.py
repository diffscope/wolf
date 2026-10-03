#!/usr/bin/env python3
"""Converts the legacy G2pPackage suites to the package format that the synthrt main branch loads.

The source is a synthrt checkout, read at the commit pinned in SOURCE_REF so that a conversion
remains reproducible after the source branch is deleted. Output is written to an untracked
directory, because these resources amount to hundreds of thousands of lines and belong in a release
rather than in the repository history.

The conversion adds no authored content. The legacy suites contain G2P modules only, because in
the legacy stack the voicebank supplied the S2P dictionary and the onset rules. Each suite therefore
converts to its inference modules, except the suites of CLOSED_LANGUAGES: their G2P output already
consists of phonemes, so the conversion adds a direct S2P module and a linguist contribution
derived from the converted dictionaries (see close_language).

Two conversions do not map one to one onto a suite. Mandarin and Cantonese each shipped a hardcoded
engine with a copy of the cpp-pinyin dictionary tree. The engine resolves that tree through a
process-global setting, so two copies cannot coexist. Both suites therefore convert to chains over
a shared backend package, which carries the dictionary once, taken from the cpp-pinyin build that
this repository links instead of from the legacy copies.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from declarations import ROLE_G2P, ROLE_S2P

# Pinned so that the conversion remains reproducible after the source branch is deleted. Without
# the pin, the converted content would survive only inside the release archives.
SOURCE_REF = "814bf81e6cd86b6670b2635032d842a997001a55"
SOURCE_SUBDIR = "resources/G2pPackages"

# Maps each legacy plugin key to the variant that replaces it. The two hardcoded pinyin engines
# become chains over the shared backend instead of separate modules.
VARIANTS = {
    "g2p.template.MandarinG2pInference": "pipe-chain",
    "g2p.template.CantoneseG2pInference": "pipe-chain",
    "g2p.chain.ChainG2pInference": "pipe-chain",
    "g2p.model.Multig2pInference": "multig2p-onnx",
}

# Maps each legacy engine plugin key to the contract pair it serves and the cpp-pinyin dictionary
# directory it reads. The directory name is internal to the engine and does not appear outside the
# backend module.
PINYIN_ENGINES = {
    "g2p.template.MandarinG2pInference": ("cmn", "pinyin", "mandarin"),
    "g2p.template.CantoneseG2pInference": ("yue", "jyutping", "cantonese"),
}

# Maps each suite to the package it becomes. Num, Punc and Unknown are omitted intentionally: all
# three are passthrough suites that differ only in a regular expression, and the authored
# wolf/lang-zxx supersedes them.
PACKAGES = {
    "Phonetic-Suite-Cmn": ("wolf/lang-cmn", "Mandarin"),
    "Phonetic-Suite-Deu": ("wolf/lang-deu", "German"),
    "Phonetic-Suite-Eng": ("wolf/lang-eng", "English"),
    "Phonetic-Suite-Fil": ("wolf/lang-fil", "Filipino"),
    "Phonetic-Suite-Fra": ("wolf/lang-fra", "French"),
    "Phonetic-Suite-Ita": ("wolf/lang-ita", "Italian"),
    "Phonetic-Suite-Jpn": ("wolf/lang-jpn", "Japanese"),
    "Phonetic-Suite-Kor": ("wolf/lang-kor", "Korean"),
    "Phonetic-Suite-Por": ("wolf/lang-por", "Portuguese"),
    "Phonetic-Suite-Rus": ("wolf/lang-rus", "Russian"),
    "Phonetic-Suite-Spa": ("wolf/lang-spa", "Spanish"),
    "Phonetic-Suite-Yue": ("wolf/lang-yue", "Cantonese"),
    "Phonetic-Suite-Multi": ("wolf/g2p-multi", "Multilingual G2P backend"),
}

BACKEND_REF = "wolf/g2p-multi:inference/multig2p"

# Languages whose notation is named (A49) and whose G2P already produces space-delimited phonemes,
# so that a direct S2P module completes the phoneme stage. These languages convert to complete
# language closures: a linguist contribution that binds a G2P and an S2P module.
#
# The remaining languages are not closed. Five still use a placeholder scheme (P7). cmn, yue and
# jpn produce syllables rather than phonemes, and their syllable-to-phoneme dictionary belongs to
# the voicebank rather than to the language package (A66); their packages therefore carry only
# inference modules, which is the final form.
CLOSED_LANGUAGES = {"eng", "por", "kor", "ita"}

# The notation of the phoneme set of each language package. Only the closed languages require a
# scheme at present. The other languages are listed so that this table remains the single record
# of these notations and the five placeholders are explicit.
SCHEMES = {
    "cmn": "pinyin", "yue": "jyutping", "jpn": "romaji", "eng": "arpabet",
    "por": "xsampa", "kor": "romaja", "ita": "xsampa-geminate",
    "deu": "ds", "fra": "ds", "spa": "ds", "rus": "ds", "fil": "ds",
}

# The notation of each language reference of the shared model.
#
# Keyed by the full reference rather than by the language, because a bundle can carry several
# phoneme sets for one language, and those sets are separate notations. Naming a set after a set it
# supposedly extends would assert a derivation that does not exist: marzipan and millefeuille are
# siblings of the default sets, not derivatives of them. arpabet-plus is the only actual derivative
# here; it is ARPABET with four added symbols.
#
# Five values are still "ds", which the naming rule does not allow, because "ds" identifies the
# users of a set rather than its contents. These sets are named once their contents are identified
# (P7).
BUNDLE_SCHEMES = {
    "eng/default": "arpabet",
    "eng/plus": "arpabet-plus",
    "deu/marzipan": "marzipan",
    "fra/millefeuille": "millefeuille",
    # Every characteristic symbol is X-SAMPA, nasality included: a~ e~ i~ o~ u~ w~ j~.
    "por/default": "xsampa",
    # Revised Romanization: eo and eu for the vowels, jj kk pp ss tt for the tense consonants,
    # and letter case distinguishing an onset from a coda.
    "kor/default": "romaja",
    # X-SAMPA as the base notation, with consonants doubled for gemination (dZZ tSS JJ LL SS EE OO).
    "ita/default": "xsampa-geminate",
    "deu/default": "ds",
    "fra/default": "ds",
    "fil/default": "ds",
    "rus/default": "ds",
    "spa/default": "ds",
}
PINYIN_ID = "wolf/g2p-pinyin"
PINYIN_REF = "wolf/g2p-pinyin:inference/pinyin"


# The revision of this pipeline that produced a package, stored as the fourth version component.
# The first three components identify the source resource, and the fourth identifies this packaging
# of it.
#
# Increment it whenever the conversion emits different output for unchanged input. Otherwise two
# different packages would carry the same version, and a consumer targeting that version would
# receive whichever package it downloaded.
# Revision 2 adds `openSet` to the emitted declarations (C1). An added optional field with a
# default does not break consumers, so the compatibility floor is unchanged.
# Revision 3 lowers the compatVersion of the language packages to the revision 0 floor, which the
# backends already used. Nothing else changes, and the change is compatible in both directions: a
# consumer targeting revision 2 remains inside the new interval, and a consumer targeting the floor
# is served by every later revision instead of by exactly one.
# Revision 4 adds the katakana spelling of the kana keys to every chain dictionary. The Japanese
# table is written in hiragana while its chain declares both kana blocks, so a katakana word was
# classified as Japanese, missed by the dictionary, and passed through unchanged by the fallback
# step without a diagnostic. The added rows are derived rather than authored: no reading from the
# source changes, and a table without kana is emitted unchanged. An added optional reading does not
# break consumers, so the compatibility floor is unchanged.
PACKAGING_REVISION = 4


def four_part(version: str, revision: int = None) -> str:
    parts = (version.split(".") + ["0", "0", "0"])[:3]
    return ".".join(parts + [str(PACKAGING_REVISION if revision is None else revision)])


def compat_floor(version: str) -> str:
    """Returns the oldest packaging revision that a consumer may target and still be served.

    Revisions differ in packaging, not in what a dependent binds to: the module ref and its
    contract are unchanged, so a package built against revision 0 is still served by revision 3.
    Declaring this interval prevents every dependent from requiring a rebuild each time the pipeline
    emits new output. The alternative, compatVersion equal to version, makes a single exact version
    the only acceptable target and turns every packaging change into a coordinated break.

    The rule is a requirement, not a convenience. The upper specification defines compatVersion as
    a guarantee about six named public surfaces (contribution categories and module ids, module
    interface and level, declared exports semantics, existing options spellings, runtime contract,
    category required fields) and requires raising it only if one of them breaks. A packaging
    revision breaks none of them, so raising the floor would state something untrue, and the
    specification classifies an untrue compatibility declaration as a package defect.

    The rule applies to every package this pipeline emits, including language packages. An earlier
    version exempted language packages on the grounds that no package depends on them. However, a
    voicebank that supplies the linguist and S2P for cmn, yue or jpn depends on exactly such a
    package, which is the structure the fixture generator produces. Moreover, the existence of
    dependents does not affect what a package declares about itself.

    A revision that changes the shape of a declaration is a breaking update under the release
    discipline and must raise this floor manually.
    """
    return four_part(version, revision=0)


def git_show(repo: Path, path: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(repo), "show", f"{SOURCE_REF}:{path}"],
        check=True, capture_output=True).stdout


def list_tree(repo: Path, path: str) -> list[str]:
    out = subprocess.run(
        ["git", "-C", str(repo), "ls-tree", "-r", "--name-only", SOURCE_REF, path],
        check=True, capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line]


def convert_verify_entries(entries: list) -> list:
    """Converts legacy tagger entries to verify entries.

    The chain called this tagger with an action, and the pinyin engines called it verify with a
    mode. After the port a single component serves both, so a single spelling (mode) is emitted.
    """
    converted = []
    for entry in entries:
        item = {"type": entry["type"], "value": entry["value"]}
        # The legacy runtime never read tag.
        item["mode"] = entry.get("mode", entry.get("action", "convert"))
        converted.append(item)
    return converted


def convert_chain_step(step: dict) -> dict | None:
    kind = step.get("step")
    params = dict(step.get("params") or {})
    out: dict = {}

    # The legacy configuration had two independent enabled flags with the same meaning. Both are
    # read, a disabled step is omitted, and no enabled flag is emitted.
    enabled = step.get("enabled", True) and params.pop("enabled", True)
    if not enabled:
        return None

    if kind == "tagAndValidate":
        out["step"] = "verify"
        out["params"] = {"entries": convert_verify_entries(params.get("tagger", []))}
    elif kind == "dict":
        out["step"] = "dict"
        out["params"] = {"file": params["file"]}
    elif kind == "format":
        out["step"] = "format"
        result = {}
        cleaner = params.get("cleaner") or {}
        if cleaner.get("operations"):
            # The cleaner wrapper held a single member and therefore carried no information.
            result["operations"] = cleaner["operations"]
        for key in ("stripTrailingSpace", "addSpaceBetweenPhones"):
            if key in params:
                result[key] = params[key]
        # normalizeTones was parsed but never read.
        out["params"] = result
    elif kind == "model":
        # The backend is now named by an import role, and its language comes from the module's
        # binding instead of from a langRef that exposed a bundle-internal identifier.
        out["step"] = "model"
        result = {"role": "backend"}
        if "batchSize" in params:
            result["batchSize"] = params["batchSize"]
        out["params"] = result
    elif kind == "fallback":
        out["step"] = "fallback"
        result = {}
        for key in ("useOriginal", "defaultPronunciation"):
            if key in params:
                result[key] = params[key]
        # markFailed appeared in fixtures but was never read.
        out["params"] = result
    else:
        raise ValueError(f"unknown legacy step: {kind}")
    return out


def normalize_dictionary(data: bytes) -> tuple[bytes, dict]:
    """Rewrites a chain dictionary into the tab-separated form that the dict step requires.

    Two suites are not already in this form. The Filipino dictionary separates its columns with
    spaces; the legacy loader accepted only tabs and silently skipped every other line, so it read
    none of the 24752 entries. The Korean and English dictionaries begin with BibTeX citation lines
    marked ";;;". Both are content defects rather than parser issues, so they are corrected here and
    the reader remains strict.

    Returns the rewritten dictionary and counts of the lines read, comments removed, lines
    re-separated with a tab and lines dropped.
    """
    stats = {"lines": 0, "comments": 0, "retabbed": 0, "dropped": 0}
    out = []
    for raw in data.decode("utf-8-sig").splitlines():
        line = raw.rstrip("\r")
        if not line.strip():
            continue
        stats["lines"] += 1
        if line.startswith(";;;"):
            stats["comments"] += 1
            continue
        if "\t" in line:
            word, _, value = line.partition("\t")
        else:
            parts = line.split(None, 1)
            if len(parts) != 2:
                stats["dropped"] += 1
                continue
            word, value = parts
            stats["retabbed"] += 1
        word = word.strip()
        value = " ".join(value.split())
        if not word or not value:
            stats["dropped"] += 1
            continue
        out.append(f"{word}\t{value}")
    return ("\n".join(out) + "\n").encode("utf-8"), stats


KANA_SHIFT = 0x60
HIRAGANA_FIRST = 0x3041
HIRAGANA_LAST = 0x3096


def katakana_spelling(word: str) -> str:
    """Rewrites the hiragana in a word as katakana, character by character.

    The two scripts represent the same syllabary. Across the range that contains the readings, each
    katakana character is at a fixed offset from its hiragana counterpart, so a reading keyed in one
    script also applies to the other.
    """
    return "".join(chr(ord(char) + KANA_SHIFT)
                   if HIRAGANA_FIRST <= ord(char) <= HIRAGANA_LAST else char
                   for char in word)


def add_katakana_aliases(data: bytes) -> tuple[bytes, int]:
    """Adds the katakana spelling of every key that the table holds in hiragana.

    The classification of a chain declares the script it accepts, independently of how much of that
    script its dictionary covers. The Japanese chain declares both kana blocks and reads a table
    written only in hiragana, so a katakana word was classified as Japanese, missed by the
    dictionary, and handled by the fallback step of the chain without a diagnostic.

    Adding aliases to the table is the smallest change that makes the classification accurate, and
    the aliases are derived rather than authored: they add no reading absent from the source, and a
    table without hiragana, which includes every other language here, is returned unchanged. A
    spelling that is already present is kept, so katakana rows added to the source take precedence.

    Returns the table and the number of added aliases.
    """
    lines = data.decode("utf-8").splitlines()
    present = set()
    rows = []
    for line in lines:
        word, _, value = line.partition("\t")
        if word:
            present.add(word)
            rows.append((word, value))
    added = []
    for word, value in rows:
        alias = katakana_spelling(word)
        if alias != word and alias not in present:
            present.add(alias)
            added.append(f"{alias}\t{value}")
    if not added:
        return data, 0
    return ("\n".join(lines + added) + "\n").encode("utf-8"), len(added)


def pinyin_engine_version(repo_root: Path) -> tuple[str, str]:
    """Returns the version and the REF of the cpp-pinyin port that this repository builds against.

    The backend package carries the dictionary of that build, so its version follows the port
    version instead of being chosen independently: a dictionary and the engine that reads it form a
    single artefact.
    """
    port = repo_root / "scripts" / "vcpkg" / "ports" / "cpp-pinyin"
    version = json.loads((port / "vcpkg.json").read_text())["version"]
    ref = ""
    for line in (port / "portfile.cmake").read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("REF "):
            ref = stripped.split(None, 1)[1].strip()
            break
    return version, ref


def write_pinyin_package(dict_source: Path, out_root: Path, repo_root: Path) -> dict:
    """Writes the shared engine package and returns its record.

    The dictionary is taken from the installed cpp-pinyin port rather than from the legacy suites.
    The legacy copies were byte-identical to the engine's resource except for one stale line, which
    shows that this tree is engine payload and was never language content.
    """
    engine_version, engine_ref = pinyin_engine_version(repo_root)
    version = four_part(engine_version)
    directory = out_root / PINYIN_ID.replace("/", "-")
    if directory.exists():
        shutil.rmtree(directory)
    module_dir = directory / "inferences" / "pinyin"
    module_dir.mkdir(parents=True)

    languages = []
    language_map = []
    for language, scheme, engine in sorted(set(PINYIN_ENGINES.values())):
        source = dict_source / engine
        if not source.is_dir():
            raise SystemExit(f"the cpp-pinyin dictionary tree has no {engine} directory: {source}")
        shutil.copytree(source, module_dir / "dict" / engine)
        languages.append({"language": language, "scheme": scheme})
        language_map.append({"language": language, "scheme": scheme, "ref": engine})

    declaration = {
        "interface": "org.openvpi.wolf.inference.G2P",
        "level": 1,
        "variant": "algo-pinyin",
        "name": "cpp-pinyin engine",
        # Required by this variant: the contract pairs and the engine language map cannot be
        # derived from each other, so both are written and the interpreter reconciles them.
        "exports": {"languages": languages},
        "configuration": {
            "formatVersion": 1,
            "dictRoot": "./dict",
            "languageMap": language_map,
        },
    }
    (module_dir / "inference.json").write_text(
        json.dumps(declaration, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

    desc = {
        "$version": "1.0",
        "id": PINYIN_ID,
        "version": version,
        # A backend is a dependency target, so it declares a compatible interval rather than a
        # single version.
        "compatVersion": compat_floor(engine_version),
        "runtimeLevel": 1,
        "vendor": "wolf",
        "copyright": "Copyright (C) wolf",
        "description": "Shared cpp-pinyin G2P backend for Mandarin and Cantonese.",
        "contributions": {
            "inference": [{"id": "pinyin", "path": "./inferences/pinyin/inference.json"}]
        },
    }
    (directory / "desc.json").write_text(
        json.dumps(desc, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

    return {"id": PINYIN_ID, "version": version, "compatVersion": desc["compatVersion"],
            "directory": directory.name, "suite": "cpp-pinyin", "engineRef": engine_ref}


def pinyin_chain(legacy: dict) -> tuple[dict, list]:
    """Converts one hardcoded pinyin engine into a chain over the shared backend.

    Word classification moves from the engine into the verify step of the chain, which accepts the
    same entries; both formats already spell a classification identically, so no translation is
    needed. The fallback of the engine, which returned the original character with an error flag
    set, becomes the fallback step of the chain, which the contract can express.

    Returns the chain configuration and its imports.
    """
    steps = []
    if "verify" in legacy:
        steps.append({"step": "verify",
                      "params": {"entries": convert_verify_entries(legacy["verify"])}})
    steps.append({"step": "model", "params": {"role": "backend"}})
    steps.append({"step": "fallback", "params": {"useOriginal": True}})
    return ({"formatVersion": 1, "steps": steps},
            [{"role": "backend", "ref": PINYIN_REF}])


def convert_configuration(variant: str, legacy: dict) -> tuple[dict, list]:
    """Returns the module configuration and any imports it needs."""
    if variant == "pipe-chain":
        steps = [s for s in (convert_chain_step(s) for s in legacy["steps"]) if s]
        config = {"formatVersion": 1, "steps": steps}
        imports = []
        if any(s["step"] == "model" for s in steps):
            imports.append({"role": "backend", "ref": BACKEND_REF})
        return config, imports

    if variant == "multig2p-onnx":
        # The legacy file is a complete training configuration; the runtime read only these four
        # keys.
        inference = legacy.get("inference") or {}
        mapping = {
            "default_max_len": "maxLen",
            "default_beam_size": "beamSize",
            "default_top_k": "topK",
            "length_penalty": "lengthPenalty",
        }
        config = {new: inference[old] for old, new in mapping.items() if old in inference}
        return config, []

    raise ValueError(f"unhandled variant: {variant}")


def multig2p_languages(bundle: dict) -> tuple[list, list]:
    """Maps every language reference in the bundle to a contract pair.

    Every reference is mapped, not only the default of each language: a phoneme set without a
    mapping cannot be requested by any voicebank, and each set here is a distinct notation with its
    own name.

    Returns the contract pairs and the language map entries.
    """
    languages, mapping = [], []
    for ref in bundle["languages"]:
        language = ref.partition("/")[0]
        scheme = BUNDLE_SCHEMES.get(ref)
        if scheme is None:
            raise SystemExit(f"the bundle contains {ref}, which has no scheme in BUNDLE_SCHEMES")
        pair = {"language": language, "scheme": scheme}
        if pair in languages:
            raise SystemExit(f"{ref} maps to {language}/{scheme}, which is already assigned to "
                             f"another reference")
        languages.append(pair)
        mapping.append({**pair, "ref": ref})
    if not mapping:
        raise SystemExit("the bundle contains no languages")
    return languages, mapping


def normalize_bundle(data: bytes) -> bytes:
    """Rewrites bundle_version as the positive integer generation that the reader expects.

    The exported bundles store it as "1.0", a string that resembles a two-part version but
    identifies a single resource generation. Every other resource format version in this family is
    an integer compared against the versions the reader supports, and a string does not support
    that comparison.
    """
    bundle = json.loads(data.decode("utf-8-sig"))
    raw = bundle.get("bundle_version")
    if isinstance(raw, str):
        bundle["bundle_version"] = int(float(raw))
    elif not isinstance(raw, int):
        raise SystemExit(f"bundle.json has an unusable bundle_version: {raw!r}")
    return (json.dumps(bundle, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def unbounded(configuration: dict) -> bool:
    """Returns whether a chain can produce output outside every list it declares.

    A single step suffices: a fallback step that returns the word unchanged. The output then
    includes arbitrary user input, so no list of phonemes can be complete, and declaring a closed
    set would state something untrue.
    """
    for step in configuration.get("steps", []):
        if step.get("step") == "fallback" and step.get("params", {}).get("useOriginal"):
            return True
    return False


def close_language(directory: Path, language: str, scheme: str, dictionaries: set[str],
                   open_set: bool) -> tuple[list, str]:
    """Adds the phoneme stage and the linguist contribution that make a package a complete language.

    The G2P of these languages already produces space-delimited phonemes, so the phoneme stage only
    splits on spaces. This is the direct variant, which requires no resource.

    The phonemes list of the linguist is the inventory of the language, collected from its
    dictionaries. Output of the model, and words that the chain returns unchanged because no step
    converts them, can fall outside this inventory. `openSet` declares that case, and its value is
    derived from the chain instead of relying on the author.

    Returns the added inference contributions and the linguist id.
    """
    phonemes = set()
    for name in sorted(dictionaries):
        for line in (directory / "inferences" / "g2p" / name).read_text(
                encoding="utf-8").splitlines():
            if "\t" in line:
                phonemes.update(line.split("\t", 1)[1].split())
    if not phonemes:
        raise SystemExit(f"{language}: the dictionaries contain no phonemes")

    pair = {"language": language, "scheme": scheme}
    s2p_dir = directory / "inferences" / "s2p"
    s2p_dir.mkdir(parents=True, exist_ok=True)
    (s2p_dir / "inference.json").write_text(json.dumps({
        "interface": "org.openvpi.wolf.inference.S2P",
        "level": 1,
        "variant": "direct",
        "name": f"{language} S2P",
        # A direct module accepts any notation, but a direct module shipped in a language package
        # serves that language. Declaring the pair enables the pair check at load time instead of
        # leaving the host to report a warning.
        "exports": {"languages": [pair]},
        "configuration": {},
    }, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

    linguist_id = f"{language}-{scheme}"
    linguist_dir = directory / "linguists" / linguist_id
    linguist_dir.mkdir(parents=True, exist_ok=True)
    (linguist_dir / "linguist.json").write_text(json.dumps({
        "interface": "org.openvpi.wolf.linguist.WolfLinguist",
        "level": 1,
        "variant": "wolf",
        "name": language,
        "language": language,
        "scheme": scheme,
        "exports": {"phonemes": sorted(phonemes), "openSet": open_set},
        "configuration": {},
        # No onset import: the contract makes the onset stage optional, and no rule resource
        # exists for these languages.
        "imports": [
            {"role": ROLE_G2P, "ref": ":inference/g2p"},
            {"role": ROLE_S2P, "ref": ":inference/s2p"},
        ],
    }, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

    return [{"id": "s2p", "path": "./inferences/s2p/inference.json"}], linguist_id


def convert_suite(repo: Path, suite: str, out_root: Path, pinyin_floor: str,
                  multi_floor: str) -> dict:
    package_id, display = PACKAGES[suite]
    legacy = json.loads(git_show(repo, f"{SOURCE_SUBDIR}/{suite}/package.json"))
    version = four_part(legacy["version"])

    directory = out_root / package_id.replace("/", "-")
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)

    contributions = []
    dependencies = []
    notes = []
    chain_dictionaries: set[str] = set()
    chain_open: dict[str, bool] = {}
    for module in legacy["modules"]["g2p"]:
        variant = VARIANTS[module["class"]]
        engine = PINYIN_ENGINES.get(module["class"])
        module_id = "multig2p" if variant == "multig2p-onnx" else "g2p"
        config_path = module["configuration"]
        legacy_dir = str(Path(config_path).parent)

        legacy_config = json.loads(git_show(repo, f"{SOURCE_SUBDIR}/{suite}/{config_path}"))
        legacy_config = legacy_config.get("configuration", legacy_config)
        bundle = None
        if engine:
            configuration, imports = pinyin_chain(legacy_config)
            backend_id, backend_version = PINYIN_ID, pinyin_floor
        else:
            configuration, imports = convert_configuration(variant, legacy_config)
            backend_id, backend_version = "wolf/g2p-multi", multi_floor

        target_dir = directory / "inferences" / module_id
        target_dir.mkdir(parents=True, exist_ok=True)

        # The files that the chain reads as dictionaries. Only these files are normalised, and the
        # language closure below reads the phoneme inventory from them.
        dictionaries = {Path(step["params"]["file"]).name
                        for step in configuration.get("steps", [])
                        if step["step"] == "dict"}
        chain_dictionaries |= dictionaries

        # Every file beside the legacy configuration file is a resource of that module, except the
        # engine dictionary of a pinyin suite, which the backend package now carries once.
        for path in list_tree(repo, f"{SOURCE_SUBDIR}/{suite}/{legacy_dir}"):
            name = path[len(f"{SOURCE_SUBDIR}/{suite}/{legacy_dir}/"):]
            if name == Path(config_path).name:
                continue
            if engine and name.startswith("dict/"):
                continue
            content = git_show(repo, path)
            if name in dictionaries:
                content, stats = normalize_dictionary(content)
                content, aliases = add_katakana_aliases(content)
                stats["katakanaAliases"] = aliases
                if (stats["comments"] or stats["retabbed"] or stats["dropped"]
                        or aliases):
                    notes.append(f"{name}: {stats}")
            elif name == "bundle.json":
                content = normalize_bundle(content)
                bundle = json.loads(content)
            destination = target_dir / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)

        declaration = {
            "interface": "org.openvpi.wolf.inference.G2P",
            "level": 1,
            "variant": variant,
            "name": f"{display} G2P",
            "configuration": configuration,
        }
        language = package_id.rsplit("-", 1)[-1]
        if variant == "pipe-chain":
            chain_open[module_id] = unbounded(configuration)
            if language in CLOSED_LANGUAGES:
                declaration["exports"] = {
                    "languages": [{"language": language, "scheme": SCHEMES[language]}],
                    # Derived rather than authored: a chain that ends by returning the word
                    # unchanged can output any string, and deriving the value removes the
                    # dependence on the author.
                    "openSet": chain_open[module_id],
                }
        if variant == "multig2p-onnx":
            # Required by this variant and reconciled against languageMap at load time: the
            # contract pairs and the bundle language references cannot be derived from each
            # other.
            if bundle is None:
                raise SystemExit(f"{suite}: the multig2p module has no bundle.json")
            languages, mapping = multig2p_languages(bundle)
            declaration["exports"] = {"languages": languages}
            configuration["languageMap"] = mapping
        if imports:
            declaration["imports"] = imports
            dependencies.append({"id": backend_id, "version": backend_version})
        (target_dir / "inference.json").write_text(
            json.dumps(declaration, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

        contributions.append({"id": module_id, "path": f"./inferences/{module_id}/inference.json"})

    language = package_id.rsplit("-", 1)[-1]
    linguist_contributions = []
    if language in CLOSED_LANGUAGES:
        extra, linguist_id = close_language(directory, language, SCHEMES[language],
                                            chain_dictionaries, any(chain_open.values()))
        contributions += extra
        linguist_contributions = [
            {"id": linguist_id, "path": f"./linguists/{linguist_id}/linguist.json"}
        ]

    # Package-level assets, referenced by modules through ../../assets at the same depth as in the
    # legacy layout.
    for path in list_tree(repo, f"{SOURCE_SUBDIR}/{suite}/assets"):
        name = path[len(f"{SOURCE_SUBDIR}/{suite}/assets/"):]
        destination = directory / "assets" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(git_show(repo, path))

    desc = {
        "$version": "1.0",
        "id": package_id,
        "version": version,
        # Every package declares a compatible interval rather than its current version. See
        # compat_floor.
        "compatVersion": compat_floor(legacy["version"]),
        "runtimeLevel": 1,
        "vendor": legacy.get("vendor", "wolf"),
        "copyright": legacy.get("copyright", "Copyright (C) wolf"),
        "description": f"{display} G2P resources.",
        "contributions": {"inference": contributions},
    }
    if linguist_contributions:
        # Declared before the inference modules that the linguist binds, matching the order in the
        # authored packages.
        desc["contributions"] = {"linguist": linguist_contributions,
                                 "inference": contributions}
    if dependencies:
        desc["dependencies"] = dependencies
    (directory / "desc.json").write_text(
        json.dumps(desc, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")

    record = {"id": package_id, "version": version, "directory": directory.name,
              "suite": suite}
    if notes:
        record["normalized"] = notes
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--synthrt", required=True, type=Path,
                        help="path to a synthrt checkout that contains SOURCE_REF")
    parser.add_argument("--out", required=True, type=Path, help="output directory (untracked)")
    parser.add_argument("--cpp-pinyin-dict", required=True, type=Path,
                        help="the installed cpp-pinyin dictionary tree, containing mandarin/ and "
                             "cantonese/ (vcpkg: share/cpp-pinyin/dict)")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    args.out.mkdir(parents=True, exist_ok=True)

    pinyin = write_pinyin_package(args.cpp_pinyin_dict, args.out, repo_root)
    print(f"built {pinyin['id']} {pinyin['version']} from {args.cpp_pinyin_dict}")

    multi_source = json.loads(
        git_show(args.synthrt, f"{SOURCE_SUBDIR}/Phonetic-Suite-Multi/package.json"))["version"]

    converted = [pinyin]
    for suite in sorted(PACKAGES):
        record = convert_suite(args.synthrt, suite, args.out, pinyin["compatVersion"],
                               compat_floor(multi_source))
        converted.append(record)
        print(f"converted {suite} -> {record['id']} {record['version']}")
        for note in record.get("normalized", []):
            print(f"    normalized {note}")

    (args.out / "converted.json").write_text(
        json.dumps({"sourceRef": SOURCE_REF, "cppPinyinRef": pinyin["engineRef"],
                    "packages": converted}, indent=4) + "\n",
        encoding="utf-8")
    print(f"\n{len(converted)} packages written to {args.out}")
    print(f"source ref {SOURCE_REF}")
    print(f"cpp-pinyin ref {pinyin['engineRef']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
