"""Definitions shared by the scripts that read or write package declarations.

Each name here mirrors a definition in the C++ sources, which are the single source of truth for
the loader. The comment beside each definition names the C++ definition it copies, so that a change
on the C++ side is repeated in exactly one place.
"""

import json
import pathlib
import re

# The import roles a wolf linguist composition binds. Mirrors ROLE_G2P, ROLE_S2P and ROLE_ONSET in
# include/wolf/Api/Linguists/Linguist/1/LinguistApiL1.h.
ROLE_G2P = "linguist/g2p"
ROLE_S2P = "linguist/s2p"
ROLE_ONSET = "linguist/onset"
LINGUIST_ROLES = (ROLE_G2P, ROLE_S2P, ROLE_ONSET)

# The contract interfaces. Mirror API_INTERFACE in the Level 1 headers under include/wolf/Api.
LINGUIST_INTERFACE = "org.openvpi.wolf.linguist.WolfLinguist"
G2P_INTERFACE = "org.openvpi.wolf.inference.G2P"
S2P_INTERFACE = "org.openvpi.wolf.inference.S2P"
ONSET_INTERFACE = "org.openvpi.wolf.inference.Onset"

# The interface that the target of each role must implement. Mirrors validateLinguistRole() in
# src/plugins/linguistproviders/wolf/WolfLinguistProvider.cpp.
ROLE_INTERFACES = {
    ROLE_G2P: G2P_INTERFACE,
    ROLE_S2P: S2P_INTERFACE,
    ROLE_ONSET: ONSET_INTERFACE,
}

# The category names. Mirror srt::SingerCategory::NAME, srt::InferenceCategory::NAME and
# wolf::LINGUIST_CATEGORY.
SINGER_CATEGORY = "singer"
INFERENCE_CATEGORY = "inference"
LINGUIST_CATEGORY = "linguist"


def load_json(path):
    """Reads a JSON file with the rules the loader applies (spec 2.4 JSON profile).

    A UTF-8 BOM is allowed, `//` and `/* */` comments are allowed outside strings, and a
    repeated key makes the whole document invalid, since which value wins would otherwise depend
    on the parser. Every failure raises ValueError naming the file.
    """
    text = pathlib.Path(path).read_text(encoding="utf-8-sig")
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise ValueError(f"{path}: unterminated comment")
            i = j + 2
        else:
            out.append(c)
            i += 1

    def no_duplicates(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"{path}: key {key!r} appears twice")
            result[key] = value
        return result

    try:
        return json.loads("".join(out), object_pairs_hook=no_duplicates)
    except json.JSONDecodeError as error:
        raise ValueError(f"{path}: not valid JSON: {error}") from None


def parse_version(text):
    """Returns the version as a tuple of four integers, or None if the loader rejects it.

    Mirrors readVersion() in synthrt's PackageLoader.cpp: one to four components separated by
    dots, each a non-empty run of ASCII digits without a leading zero. Missing trailing components
    are treated as zero, consistent with the loader's comparison of versions of different lengths.
    """
    if not isinstance(text, str) or not text:
        return None
    parts = text.split(".")
    if len(parts) > 4:
        return None
    for part in parts:
        if not part or not all("0" <= ch <= "9" for ch in part):
            return None
        if len(part) > 1 and part[0] == "0":
            return None
    numbers = [int(part) for part in parts]
    return tuple((numbers + [0, 0, 0, 0])[:4])


SEGMENT = re.compile(r"[A-Za-z0-9_-]+")


def is_package_id(text):
    """Returns whether the loader accepts a package id.

    Mirrors ContribLocator::isValidPackageId() in synthrt: one or more segments of ASCII letters,
    digits, `_` and `-`, separated by `/`.
    """
    return isinstance(text, str) and bool(text) and all(
        SEGMENT.fullmatch(segment) for segment in text.split("/"))


def schema_pattern(pattern):
    """Compiles a JSON Schema pattern with the matching semantics that JSON Schema specifies.

    A schema pattern is an ECMA-262 expression applied as a search, and `$` in it matches only at
    the very end of the string. Python's `$` also matches before a final newline, so a pattern
    anchored with `$` would accept "cmn\\n" although the schema rejects it; a trailing `$` is
    therefore rewritten as `\\Z`. The schemas here use no other construct whose meaning differs
    between the two dialects.
    """
    if pattern.endswith("$") and not pattern.endswith("\\$"):
        pattern = pattern[:-1] + r"\Z"
    return re.compile(pattern)


def split_ref(ref):
    """Splits an import ref into (package id, category, contribution id).

    The package id is empty for a ref into the referring package (`:category/id`). Returns None for
    a ref of any other shape. Mirrors the split in ContribLocator::fromString() in synthrt: exactly
    one `:` and, after it, exactly one `/`.
    """
    if not isinstance(ref, str) or ref.count(":") != 1:
        return None
    package, _, rest = ref.partition(":")
    if rest.count("/") != 1:
        return None
    category, _, contribution = rest.partition("/")
    if not category or not contribution:
        return None
    return package, category, contribution
