#!/usr/bin/env python3
"""Convert the legacy G2pPackage suites to the package format synthrt main loads.

The source is a synthrt checkout, read at a pinned commit so a conversion can be reproduced after
that branch is gone. Output goes to a directory that is not tracked: these resources run to
hundreds of thousands of lines and belong in a release, not in history.

What this does not do is invent content. The legacy suites carry G2P modules only, because in the
old stack the S2P dictionary and onset rules came from the voicebank rather than from the language
package. So each suite converts to its inference modules and nothing else; the linguist
contribution that would make a package a complete language waits for that content to exist.

Two conversions are not one to one with a suite. Mandarin and Cantonese shipped one hardcoded
engine each, with a copy of the cpp-pinyin dictionary tree beside it; the engine resolves that tree
through a process global, so two copies cannot coexist. They convert to chains over a shared
backend package instead, and that package carries the dictionary once, taken from the engine build
this repository links rather than from the legacy copies.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

# Pinned so the conversion stays reproducible once the branch is gone. Its only other copy would
# be inside the release archives themselves.
SOURCE_REF = "814bf81e6cd86b6670b2635032d842a997001a55"
SOURCE_SUBDIR = "resources/G2pPackages"

# Legacy plugin key to the variant that replaces it. The two hardcoded pinyin engines become
# chains over the shared backend rather than modules of their own.
VARIANTS = {
    "g2p.template.MandarinG2pInference": "pipe-chain",
    "g2p.template.CantoneseG2pInference": "pipe-chain",
    "g2p.chain.ChainG2pInference": "pipe-chain",
    "g2p.model.Multig2pInference": "multig2p-onnx",
}

# Legacy engine plugin key to the contract pair it serves and the cpp-pinyin dictionary directory
# behind it. The directory name is internal to the engine and never leaves the backend module.
PINYIN_ENGINES = {
    "g2p.template.MandarinG2pInference": ("cmn", "pinyin", "mandarin"),
    "g2p.template.CantoneseG2pInference": ("yue", "jyutping", "cantonese"),
}

# Suite to the package it becomes. Num, Punc and Unknown are absent on purpose: all three are pure
# passthrough differing only in a regex, and they are superseded by the authored wolf/lang-zxx.
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

# Languages whose notation is named (A49) and whose G2P already produces space delimited phonemes,
# so a direct S2P is the whole of what the phoneme stage has to do. These convert to complete
# language closures: a linguist contribution binding a G2P and an S2P.
#
# The rest wait. Five still carry a placeholder scheme (P7), and cmn, yue and jpn produce
# syllables rather than phonemes, so their S2P needs a real syllable dictionary that does not
# exist yet (B3).
CLOSED_LANGUAGES = {"eng", "por", "kor", "ita"}

# The notation each language package's own phoneme set is written in. Only the closed languages
# need one today; the rest are listed so the table stays the single place this is written down,
# with the five placeholders visible rather than implied.
SCHEMES = {
    "cmn": "pinyin", "yue": "jyutping", "jpn": "romaji", "eng": "arpabet",
    "por": "xsampa", "kor": "romaja", "ita": "xsampa-geminate",
    "deu": "ds", "fra": "ds", "spa": "ds", "rus": "ds", "fil": "ds",
}

# The notation behind each of the shared model's own language references.
#
# Keyed by the whole reference, not by the language: a bundle can carry several phoneme sets for
# one language, and those are separate notations rather than one notation with a footnote. Naming
# them by the set they extend would claim a descent that is not there — marzipan and millefeuille
# are siblings of the default sets, not derivatives of them. arpabet-plus is the one real
# derivative here, being ARPABET with four symbols added.
#
# Five values are still "ds", which the naming rule does not allow: it says who uses a set rather
# than what it is. They are named once someone can say what those sets are (P7).
BUNDLE_SCHEMES = {
    "eng/default": "arpabet",
    "eng/plus": "arpabet-plus",
    "deu/marzipan": "marzipan",
    "fra/millefeuille": "millefeuille",
    # Every characteristic symbol is X-SAMPA, nasality included: a~ e~ i~ o~ u~ w~ j~.
    "por/default": "xsampa",
    # Revised Romanization: eo and eu for the vowels, jj kk pp ss tt for the tense consonants,
    # and case telling an onset from a coda.
    "kor/default": "romaja",
    # X-SAMPA underneath, with consonants doubled for gemination (dZZ tSS JJ LL SS EE OO).
    "ita/default": "xsampa-geminate",
    "deu/default": "ds",
    "fra/default": "ds",
    "fil/default": "ds",
    "rus/default": "ds",
    "spa/default": "ds",
}
PINYIN_ID = "wolf/g2p-pinyin"
PINYIN_REF = "wolf/g2p-pinyin:inference/pinyin"


# Which revision of this pipeline produced a package. It is the fourth version component, so
# the first three keep naming the source resource and this one names our packaging of it.
#
# Bump it whenever the conversion emits something different for unchanged input. Without that,
# two different packages would carry one version, and a consumer targeting that version would
# get whichever it happened to download.
# Revision 2 adds `openSet` to the declarations it emits (C1). An added optional field with a
# default is not breaking for a consumer, so the compatibility floor stays where it is.
# Revision 3 lowers the language packages' compatVersion to the revision 0 floor, the same one the
# backends already used. Nothing else changes, and it is not breaking in either direction: a
# consumer targeting revision 2 is still inside the new interval, and one targeting the floor is
# served by every revision from here on instead of by exactly one.
# Revision 4 gives every chain dictionary the katakana spelling of the kana keys it holds. The
# Japanese table is written in hiragana while its chain declares both kana blocks, so a katakana
# word was classified as Japanese and then missed by the dictionary, which the chain's fallback
# turned into a silent pass through. The added rows are derived, not authored: no reading the
# source carried is changed, and a table with no kana in it is emitted exactly as before. An
# added optional reading is not breaking for a consumer, so the compatibility floor stays put.
PACKAGING_REVISION = 4


def four_part(version: str, revision: int = None) -> str:
    parts = (version.split(".") + ["0", "0", "0"])[:3]
    return ".".join(parts + [str(PACKAGING_REVISION if revision is None else revision)])


def compat_floor(version: str) -> str:
    """The oldest packaging revision a consumer may target and still be served.

    Revisions differ in packaging, not in what a dependent binds to: the module ref and its
    contract are unchanged, so a package built against revision 0 is still served by revision 3.
    Saying so is what keeps every dependent from needing a rebuild each time the pipeline emits
    something new — the alternative, compatVersion equal to version, makes one exact point the
    only acceptable target and turns a packaging change into a flag day.

    This is not a convenience. The upper specification defines compatVersion as a promise about
    six named public surfaces (contribution categories and module ids, module interface and level,
    declared exports semantics, existing options spellings, runtime contract, category required
    fields) and requires raising it only when one of them breaks. A packaging revision breaks none
    of them, so raising it states something untrue, and the specification calls an untrue promise a
    package defect.

    It applies to every package this pipeline emits, language packages included. An earlier version
    exempted them on the grounds that nothing depends on a language package — but a voicebank that
    supplies the linguist and S2P for cmn, yue or jpn depends on exactly that, which is the shape
    the fixture generator produces, and whether anyone depends on a package is not an input to a
    promise it makes about itself.

    A revision that does change a declaration's shape is a breaking update by the release
    discipline and must raise this floor by hand.
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
    """The chain called this tagger with an action; the pinyin engines called it verify with a
    mode. One component serves both after the port, so one spelling does too."""
    converted = []
    for entry in entries:
        item = {"type": entry["type"], "value": entry["value"]}
        # tag was never read by anything.
        item["mode"] = entry.get("mode", entry.get("action", "convert"))
        converted.append(item)
    return converted


def convert_chain_step(step: dict) -> dict | None:
    kind = step.get("step")
    params = dict(step.get("params") or {})
    out: dict = {}

    # The old configuration had two independent enabled flags meaning the same thing. Only the
    # step level one survives.
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
            # The cleaner wrapper held one member and so carried no information.
            result["operations"] = cleaner["operations"]
        for key in ("stripTrailingSpace", "addSpaceBetweenPhones"):
            if key in params:
                result[key] = params[key]
        # normalizeTones was parsed and never read.
        out["params"] = result
    elif kind == "model":
        # The backend is named by an import role now, and its language comes from the module's own
        # binding rather than from a langRef that leaked a bundle-internal identifier.
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
        # markFailed appeared in fixtures and was never read.
        out["params"] = result
    else:
        raise ValueError(f"unknown legacy step: {kind}")
    return out


def normalize_dictionary(data: bytes) -> tuple[bytes, dict]:
    """Rewrites a chain dictionary into the tab separated form the dict step declares.

    Two suites do not already hold it. The Filipino dictionary separates its columns with spaces,
    which means the legacy loader — tab only, silently skipping any other line — read none of its
    24752 entries; the Korean and English ones open with BibTeX citation lines marked ";;;". Both
    are content defects rather than parser questions, so they are fixed here and the reader stays
    strict.
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
    """Rewrites the hiragana in a word as katakana, letter by letter.

    The two scripts are one syllabary: across the range that carries the readings, katakana sits at
    a fixed offset from hiragana, so a reading keyed in one script answers the other.
    """
    return "".join(chr(ord(char) + KANA_SHIFT)
                   if HIRAGANA_FIRST <= ord(char) <= HIRAGANA_LAST else char
                   for char in word)


def add_katakana_aliases(data: bytes) -> tuple[bytes, int]:
    """Adds the katakana spelling of every key the table already holds in hiragana.

    A chain's classification declares the script it accepts, not the fraction of it its dictionary
    happens to cover: the Japanese one declares both kana blocks and reads a table written in
    hiragana alone, so a katakana word was claimed by the language and then missed by the
    dictionary, ending in the chain's fallback with nothing said about it.

    Aliasing the table is the smallest way to make that claim true, and it is derived rather than
    authored: it adds no reading the source did not already carry, and a table with no hiragana in
    it comes back unchanged, which is every other language here. A spelling that is already present
    is left alone, so a source that grows katakana rows itself keeps them.
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
    """Reads the cpp-pinyin port this repository builds against.

    The backend package carries that build's dictionary, so its version tracks the port's rather
    than being chosen: a dictionary and the engine that reads it are one artefact.
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
    """Writes the shared engine package.

    The dictionary comes from the installed cpp-pinyin port rather than from the legacy suites.
    Those copies were byte identical to the engine's own resource apart from one stale line, which
    is the whole argument: this tree is engine payload and never was language content.
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
            raise SystemExit(f"the cpp-pinyin dictionary is missing {engine}: {source}")
        shutil.copytree(source, module_dir / "dict" / engine)
        languages.append({"language": language, "scheme": scheme})
        language_map.append({"language": language, "scheme": scheme, "ref": engine})

    declaration = {
        "interface": "org.openvpi.wolf.inference.G2P",
        "level": 1,
        "variant": "algo-pinyin",
        "name": "cpp-pinyin engine",
        # Required by this variant: the contract face and the engine face cannot be derived from
        # one another, so both are written and the interpreter reconciles them.
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
        # A backend is depended upon, so it promises an interval rather than a point.
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
    """Turns one hardcoded pinyin engine into a chain over the shared backend.

    The word classification moves out of the engine and into the chain's verify step, which takes
    the same entries: both sides already spelled a classification the same way, so nothing is
    translated here. The engine's own fallback, which handed back the original character with an
    error flag set, becomes the chain's fallback step, which the contract can express.
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
        # The legacy file is a training configuration dumped whole; the runtime only ever read
        # these four.
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
    """Maps every language reference the bundle carries onto a contract pair.

    All of them, not just the default of each language: a phoneme set nothing maps is a set no
    voicebank can ever ask for, and the sets here are real ones with names of their own.
    """
    languages, mapping = [], []
    for ref in bundle["languages"]:
        language = ref.partition("/")[0]
        scheme = BUNDLE_SCHEMES.get(ref)
        if scheme is None:
            raise SystemExit(f"the bundle carries {ref}, which has no agreed scheme")
        pair = {"language": language, "scheme": scheme}
        if pair in languages:
            raise SystemExit(f"{ref} maps onto {language}/{scheme}, which another reference took")
        languages.append(pair)
        mapping.append({**pair, "ref": ref})
    if not mapping:
        raise SystemExit("the bundle carries no languages")
    return languages, mapping


def normalize_bundle(data: bytes) -> bytes:
    """Rewrites bundle_version into the positive integer generation the reader expects.

    The exported bundles write it as "1.0", a string that looks like a two part version but names
    one resource generation. Every other resource format version in this family is an integer
    compared against what the reader supports, and a string cannot be compared that way.
    """
    bundle = json.loads(data.decode("utf-8-sig"))
    raw = bundle.get("bundle_version")
    if isinstance(raw, str):
        bundle["bundle_version"] = int(float(raw))
    elif not isinstance(raw, int):
        raise SystemExit(f"bundle.json has an unusable bundle_version: {raw!r}")
    return (json.dumps(bundle, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def unbounded(configuration: dict) -> bool:
    """Whether a chain can produce something outside any list it declares.

    One step does it: a fallback that hands the word back unchanged. After that the output is
    whatever the user typed, so no list of phonemes can be complete, and saying it is would be
    stating something untrue rather than leaving a field out.
    """
    for step in configuration.get("steps", []):
        if step.get("step") == "fallback" and step.get("params", {}).get("useOriginal"):
            return True
    return False


def close_language(directory: Path, language: str, scheme: str, dictionaries: set[str],
                   open_set: bool) -> tuple[list, str]:
    """Adds the phoneme stage and the linguist contribution that make a package a language.

    The G2P of these languages already produces space delimited phonemes, so the phoneme stage is
    a split on spaces and nothing more — the direct variant, which needs no resource.

    The linguist's phonemes list is the inventory the language works in, taken from its own
    dictionary. Words the model produced, or that the chain handed back unchanged because nothing
    could convert them, may fall outside it — which is exactly what `openSet` is for, and it is
    derived from the chain rather than left to the author to remember.
    """
    phonemes = set()
    for name in sorted(dictionaries):
        for line in (directory / "inferences" / "g2p" / name).read_text(
                encoding="utf-8").splitlines():
            if "\t" in line:
                phonemes.update(line.split("\t", 1)[1].split())
    if not phonemes:
        raise SystemExit(f"{language}: no phonemes to declare")

    pair = {"language": language, "scheme": scheme}
    s2p_dir = directory / "inferences" / "s2p"
    s2p_dir.mkdir(parents=True, exist_ok=True)
    (s2p_dir / "inference.json").write_text(json.dumps({
        "interface": "org.openvpi.wolf.inference.S2P",
        "level": 1,
        "variant": "direct",
        "name": f"{language} S2P",
        # A direct module works for any notation, but one shipped inside a language package is
        # there to serve that language: declaring the pair is what lets the pair check run at
        # load instead of leaving the host to warn.
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
        # No onset import: the contract makes that stage optional, and no rule resource exists
        # for any of these languages.
        "imports": [
            {"role": "linguist/g2p", "ref": ":inference/g2p"},
            {"role": "linguist/s2p", "ref": ":inference/s2p"},
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

        # Which files the chain reads as dictionaries, so those alone are normalised — and so
        # the language closure below can read the inventory back out of them.
        dictionaries = {Path(step["params"]["file"]).name
                        for step in configuration.get("steps", [])
                        if step["step"] == "dict"}
        chain_dictionaries |= dictionaries

        # Everything beside the legacy configuration file is a resource of that module, except the
        # engine dictionary of a pinyin suite: that tree now lives once in the backend package.
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
                    # Derived, not authored: a chain that ends by handing the word back can output
                    # anything, and an author remembering to say so is not a mechanism.
                    "openSet": chain_open[module_id],
                }
        if variant == "multig2p-onnx":
            # Required by this variant, and reconciled against languageMap when it loads: the
            # contract face and the bundle face cannot be derived from one another.
            if bundle is None:
                raise SystemExit(f"{suite}: the multig2p module carries no bundle.json")
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

    # Package level assets, referenced by modules through ../../assets at the same depth as before.
    for path in list_tree(repo, f"{SOURCE_SUBDIR}/{suite}/assets"):
        name = path[len(f"{SOURCE_SUBDIR}/{suite}/assets/"):]
        destination = directory / "assets" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(git_show(repo, path))

    desc = {
        "$version": "1.0",
        "id": package_id,
        "version": version,
        # Every package promises an interval, not the point it happens to be. See compat_floor.
        "compatVersion": compat_floor(legacy["version"]),
        "runtimeLevel": 1,
        "vendor": legacy.get("vendor", "wolf"),
        "copyright": legacy.get("copyright", "Copyright (C) wolf"),
        "description": f"{display} G2P resources.",
        "contributions": {"inference": contributions},
    }
    if linguist_contributions:
        # Declared before the inference modules it binds, which is how the authored packages read.
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
    parser.add_argument("--synthrt", required=True, type=Path, help="a synthrt checkout")
    parser.add_argument("--out", required=True, type=Path, help="output directory, not tracked")
    parser.add_argument("--cpp-pinyin-dict", required=True, type=Path,
                        help="the installed cpp-pinyin dictionary tree, holding mandarin/ and "
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
