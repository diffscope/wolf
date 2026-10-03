#!/usr/bin/env python3
"""Builds a voicebank-shaped package from the converted language packages.

For cmn and yue, the language packages carry only a G2P module: the syllable-to-phoneme dictionary
of those two languages is voicebank content rather than language content, so no package in the four
repositories contains one. A host therefore cannot exercise the complete path against the real
resources without a voicebank, and this script builds that voicebank.

The dictionary that the script writes is *not* the dictionary of any shipping voicebank. It is
derived by a documented split rule from the syllable inventories that the real resources carry
(`assets/ds-zh-pinyin-lite.txt`, `assets/jyutping_dict.txt`). Every syllable is real and every
syllable the G2P can produce is covered, which is sufficient for a test; the phoneme set of a
particular voicebank is outside the scope of this fixture.

Usage:

    python3 scripts/make-voicebank-fixture.py [--packages DIR] [--output DIR]
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

from declarations import ROLE_G2P, ROLE_ONSET, ROLE_S2P

REPOSITORY = Path(__file__).resolve().parent.parent

# Longest first: `zh` must match before `z`, so that `h` is not split off.
PINYIN_INITIALS = ["zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h",
                   "j", "q", "x", "r", "z", "c", "s", "y", "w"]

# Jyutping has two labialized initials and a syllabic nasal, so the order is significant here as
# well.
JYUTPING_INITIALS = ["gw", "kw", "ng", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h",
                     "z", "c", "s", "j", "w"]

LANGUAGES = {
    "cmn": {
        "package": "wolf-lang-cmn",
        "asset": "ds-zh-pinyin-lite.txt",
        "scheme": "pinyin",
        "initials": PINYIN_INITIALS,
        "dependency": "wolf/lang-cmn",
        # The file names that a real voicebank uses for these two resources.
        "dictionary": "opencpop-extension.txt",
        "onsets": "opencpop-extension_onset.json",
    },
    "yue": {
        "package": "wolf-lang-yue",
        "asset": "jyutping_dict.txt",
        "scheme": "jyutping",
        "initials": JYUTPING_INITIALS,
        "dependency": "wolf/lang-yue",
        "dictionary": "jyutping-extension.txt",
        "onsets": "jyutping-extension_onset.json",
    },
}


def read_syllables(path):
    """Returns the syllable inventory of an asset.

    Each asset consists of two identical tab-separated columns, so the first column is the
    inventory.
    """
    syllables = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        syllables.append(line.split("\t")[0].strip())
    if not syllables:
        raise SystemExit(f"{path} contains no syllables")
    return syllables


def split(syllable, initials):
    """Splits one syllable into at most an initial and a final.

    A syllable that consists only of an initial (the syllabic `m`, `ng`) stays whole instead of
    losing its vowel slot, which keeps every entry non-empty.
    """
    for initial in initials:
        if syllable.startswith(initial) and len(syllable) > len(initial):
            return [initial, syllable[len(initial):]]
    return [syllable]


def write_dictionary(path, syllables, initials):
    lines = []
    consonants = set()
    vowels = set()
    for syllable in sorted(set(syllables)):
        phonemes = split(syllable, initials)
        if len(phonemes) == 2:
            consonants.add(phonemes[0])
            vowels.add(phonemes[1])
        else:
            vowels.add(phonemes[0])
        lines.append(f"{syllable}\t{' '.join(phonemes)}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return consonants, vowels


def write_onset_rules(path, consonants, vowels):
    """Marks the first phoneme of every syllable.

    Two rules cover the whole inventory because the split above produces at most two phonemes:
    a consonant followed by a vowel starts a syllable at the consonant, and a lone vowel starts a
    syllable at the vowel. The longest pattern takes precedence, so the pair is tried before the
    bare vowel.
    """
    types = {}
    for phoneme in sorted(consonants):
        types[phoneme] = "consonant"
    for phoneme in sorted(vowels):
        types[phoneme] = "vowel"
    path.write_text(json.dumps({
        "phonemeTypes": types,
        "rules": [
            {"pattern": ["consonant", "vowel"], "onsets": [0]},
            {"pattern": ["vowel"], "onsets": [0]},
        ],
    }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    # The default is the tree that the tests load, so that the two cannot diverge: the fixture
    # declares a dependency on a concrete version, and building it against one tree while the tests
    # load another produces a dependency that resolves now and fails later. If the variable is
    # unset, the default is the tree that make-lang-release.py --verify unpacks, which is the tree
    # that a local build assigns to WOLF_LANG_PACKAGES_SOURCE. Both defaults are relative to the
    # repository rather than to the working directory.
    parser.add_argument("--packages", type=Path,
                        default=Path(os.environ.get("WOLF_LANG_PACKAGES_SOURCE",
                                                    REPOSITORY / "build" / "lang-packages" /
                                                    "verify")),
                        help="directory of the language packages that the tests load; defaults to "
                             "$WOLF_LANG_PACKAGES_SOURCE, otherwise build/lang-packages/verify in "
                             "the repository")
    parser.add_argument("--output", type=Path,
                        default=REPOSITORY / "build" / "voicebank-fixture",
                        help="directory to write the voicebank package to; defaults to "
                             "build/voicebank-fixture in the repository")
    args = parser.parse_args()

    root = args.output / "wolf-voicebank-zh"
    if root.exists():
        shutil.rmtree(root)

    inference_contributions = []
    linguist_contributions = []
    languages = {}
    dependencies = []
    phoneme_sets = {}

    for language, spec in sorted(LANGUAGES.items()):
        package = args.packages / spec["package"]
        asset = package / "assets" / spec["asset"]
        if not asset.is_file():
            raise SystemExit(f"missing {asset}; run scripts/convert-g2p-packages.py first")
        # Require the version that the package declares as its compatibility floor instead of a
        # version written here: the packaging revision changes, and a hardcoded version stops
        # resolving when it does.
        required = json.loads((package / "desc.json").read_text(encoding="utf-8"))["compatVersion"]

        s2p_dir = root / "inferences" / f"s2p-{language}"
        s2p_dir.mkdir(parents=True)
        consonants, vowels = write_dictionary(s2p_dir / spec["dictionary"],
                                              read_syllables(asset), spec["initials"])
        (s2p_dir / "inference.json").write_text(json.dumps({
            "interface": "org.openvpi.wolf.inference.S2P",
            "level": 1,
            "variant": "dict",
            "name": f"{language} syllables",
            "configuration": {"file": spec["dictionary"]},
            "exports": {
                "languages": [{"language": language, "scheme": spec["scheme"]}],
            },
        }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

        onset_dir = root / "inferences" / f"onset-{language}"
        onset_dir.mkdir(parents=True)
        write_onset_rules(onset_dir / spec["onsets"], consonants, vowels)
        (onset_dir / "inference.json").write_text(json.dumps({
            "interface": "org.openvpi.wolf.inference.Onset",
            "level": 1,
            "variant": "rule",
            "name": f"{language} onsets",
            "configuration": {"file": spec["onsets"]},
        }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

        linguist_id = f"{language}-{spec['scheme']}"
        linguist_dir = root / "linguists" / linguist_id
        linguist_dir.mkdir(parents=True)
        phonemes = sorted(consonants | vowels)
        phoneme_sets[language] = phonemes
        (linguist_dir / "linguist.json").write_text(json.dumps({
            "interface": "org.openvpi.wolf.linguist.WolfLinguist",
            "level": 1,
            "variant": "wolf",
            "name": language,
            "language": language,
            "scheme": spec["scheme"],
            "exports": {"phonemes": phonemes},
            "configuration": {},
            "imports": [
                # The G2P belongs to the language package; the S2P and onset modules belong to
                # the voicebank.
                {"role": ROLE_G2P, "ref": f"{spec['dependency']}:inference/g2p"},
                {"role": ROLE_S2P, "ref": f":inference/s2p-{language}"},
                {"role": ROLE_ONSET, "ref": f":inference/onset-{language}"},
            ],
        }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

        inference_contributions += [
            {"id": f"s2p-{language}", "path": f"./inferences/s2p-{language}/inference.json"},
            {"id": f"onset-{language}", "path": f"./inferences/onset-{language}/inference.json"},
        ]
        linguist_contributions.append(
            {"id": linguist_id, "path": f"./linguists/{linguist_id}/linguist.json"})
        languages[language] = f"lang/{language}"
        dependencies.append({"id": spec["dependency"], "version": required})

    singer_dir = root / "singers" / "zh"
    singer_dir.mkdir(parents=True)
    (singer_dir / "singer.json").write_text(json.dumps({
        "interface": "org.openvpi.wolf.test.Singer",
        "level": 1,
        "variant": "stub",
        "name": "Voicebank Fixture",
        "languages": languages,
        "defaultLanguage": "cmn",
        "imports": [
            {"role": f"lang/{language}",
             "ref": f":linguist/{language}-{LANGUAGES[language]['scheme']}"}
            for language in sorted(LANGUAGES)
        ],
    }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

    (root / "desc.json").write_text(json.dumps({
        "$version": "1.0",
        "id": "wolf/voicebank-zh",
        "version": "1.0.0.0",
        "runtimeLevel": 1,
        "vendor": "wolf",
        "description": "A voicebank shaped fixture over the converted cmn and yue packages.",
        "contributions": {
            "singer": [{"id": "zh", "path": "./singers/zh/singer.json"}],
            "linguist": linguist_contributions,
            "inference": inference_contributions,
        },
        "dependencies": dependencies,
    }, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")

    for language in sorted(LANGUAGES):
        print(f"{language}: {len(phoneme_sets[language])} phonemes", file=sys.stderr)
    print(f"wrote {root}", file=sys.stderr)


if __name__ == "__main__":
    main()
