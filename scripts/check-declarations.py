#!/usr/bin/env python3
"""Check module declarations against the published contract schemas, and lint what else is local.

The schemas in docs/schemas are what the specification calls the machine readable half of an
interface contract. They are only worth publishing if they agree with the loader, so this walks
real packages and validates every declaration's exports and import options against them.

Two checks from the domain contract's lint list ride along, the two that need nothing but the
declarations themselves. The other two in that list compare against a voicebank's phoneme tables
or a model's, which is editor work at run time rather than anything a packaging pass can see.

Findings come in two kinds. Errors are what the loader itself refuses: the schema checks, the
identity fields and roles a declaration must carry, and the shape of a singer's language map.
Warnings are conventions the loader does not enforce. The one rule reported as an error although
the loader accepts the package is the dependency floor (check_dependencies): a consumer that names
anything but the target's compatibility floor loads today and stops loading at the target's next
revision, so packaging refuses it while there is still time.

The validator here understands exactly the keywords those schemas use — type, properties,
required, additionalProperties, items, uniqueItems, minLength, pattern, oneOf and $ref. It is not
a general JSON Schema implementation, and says so rather than pretending: a schema using anything
else fails loudly instead of passing unchecked.
"""

import argparse
import json
import pathlib
import re
import sys
from pathlib import Path


def load_json(path):
    """Reads a JSON file the way the loader does (spec 2.4 JSON profile).

    A UTF-8 BOM is allowed, `//` and `/* */` comments are allowed outside strings, and a
    repeated key makes the whole document invalid, since which value wins would otherwise depend
    on the parser.
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

    return json.loads("".join(out), object_pairs_hook=no_duplicates)

# Anything not listed here fails loudly rather than being skipped: a keyword this validator does
# not implement would otherwise look like a rule that passed. "default" is the one exception that
# needs no implementation — JSON Schema defines it as an annotation that asserts nothing.
SUPPORTED = {
    "$id", "$ref", "$schema", "title", "description", "default",
    "type", "properties", "required", "additionalProperties",
    "items", "uniqueItems", "minLength", "pattern", "oneOf",
}

TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "integer": int,
    "number": (int, float),
    "boolean": bool,
}


class Schemas:
    def __init__(self, directory: Path):
        self.by_id = {}
        for path in sorted(directory.glob("*.schema.json")):
            body = load_json(path)
            self.by_id[body["$id"]] = body
            self.by_id[path.name] = body

    def resolve(self, ref: str) -> dict:
        if ref in self.by_id:
            return self.by_id[ref]
        tail = ref.rsplit("/", 1)[-1]
        if tail in self.by_id:
            return self.by_id[tail]
        raise SystemExit(f"unresolvable $ref: {ref}")


def validate(value, schema: dict, schemas: Schemas, where: str, errors: list) -> None:
    unknown = set(schema) - SUPPORTED
    if unknown:
        raise SystemExit(f"{schema.get('$id', where)}: unsupported schema keywords {sorted(unknown)}")

    if "$ref" in schema:
        validate(value, schemas.resolve(schema["$ref"]), schemas, where, errors)
        return

    if "oneOf" in schema:
        matched = 0
        for option in schema["oneOf"]:
            trial = []
            validate(value, option, schemas, where, trial)
            matched += not trial
        if matched != 1:
            errors.append(f"{where}: matches {matched} of the allowed forms, expected exactly one")
        return

    if "type" in schema:
        expected = TYPES[schema["type"]]
        # JSON has one number type and Python distinguishes bool from int; neither confusion is
        # wanted here.
        if isinstance(value, bool) != (schema["type"] == "boolean"):
            errors.append(f"{where}: expected {schema['type']}")
            return
        if not isinstance(value, expected):
            errors.append(f"{where}: expected {schema['type']}")
            return

    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{where}: must not be empty")
        if "pattern" in schema and not re.fullmatch(schema["pattern"].strip("^$"), value):
            errors.append(f"{where}: {value!r} does not match {schema['pattern']}")

    if isinstance(value, list):
        if schema.get("uniqueItems"):
            seen = []
            for item in value:
                if item in seen:
                    errors.append(f"{where}: repeats {item!r}")
                seen.append(item)
        if "items" in schema:
            for i, item in enumerate(value):
                validate(item, schema["items"], schemas, f"{where}[{i}]", errors)

    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{where}: missing {name}")
        properties = schema.get("properties", {})
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], schemas, f"{where}.{name}", errors)
            elif schema.get("additionalProperties") is False:
                errors.append(f"{where}: unknown key {name}")


# Which schema pair covers which contract. A declaration whose interface is not listed is not
# ours to check.
CONTRACTS = {
    ("org.openvpi.wolf.linguist.WolfLinguist", 1): "linguist-1",
    ("org.openvpi.wolf.inference.G2P", 1): "g2p-1",
    ("org.openvpi.wolf.inference.S2P", 1): "s2p-1",
    ("org.openvpi.wolf.inference.Onset", 1): "onset-1",
}


def unbounded_chain(declaration: dict) -> bool:
    """Whether a chain can produce something outside any list it declares.

    One step does it: a fallback that hands the word back unchanged. After that the output is
    whatever the user typed, and no list of symbols or phonemes can be complete.
    """
    if declaration.get("variant") != "pipe-chain":
        return False
    # useOriginal defaults to true in the loader, so a fallback that does not say is one that
    # hands the word back.
    return any(step.get("step") == "fallback" and step.get("params", {}).get("useOriginal", True)
               for step in declaration.get("configuration", {}).get("steps", []))


def local_import(declaration: dict, role: str, inferences: dict):
    """The declaration a role points at, when it points inside this package.

    A ref into another package cannot be read from here, so those are left alone rather than
    guessed at: a lint that guesses is worse than one that stays quiet.
    """
    for item in declaration.get("imports", []):
        if item.get("role") != role:
            continue
        ref = item.get("ref", "")
        if not ref.startswith(":inference/"):
            return None
        return inferences.get(ref[len(":inference/"):])
    return None


LANGUAGE_HANDLE = re.compile(r"[a-z]{3}")
SCHEME_NAME = re.compile(r"[a-z0-9]+(-[a-z0-9]+)*")


def check_identity(declaration: dict, where: str, errors: list) -> None:
    """The two identity fields the linguist category reads, with the grammar the loader applies."""
    language = declaration.get("language")
    scheme = declaration.get("scheme")
    if not isinstance(language, str) or not LANGUAGE_HANDLE.fullmatch(language):
        errors.append(f"{where}: language must be an ISO 639-3 code of three lowercase letters")
    if not isinstance(scheme, str) or not SCHEME_NAME.fullmatch(scheme):
        errors.append(f"{where}: scheme must match [a-z0-9]+(-[a-z0-9]+)*")
    roles = {item.get("role") for item in declaration.get("imports", [])}
    if "linguist/g2p" not in roles:
        errors.append(f"{where}: a linguist must import a linguist/g2p role")


def check_singer_languages(declaration: dict, where: str, errors: list) -> None:
    """The shape the loader requires of a singer's language map, when it declares one.

    Both fields sit in the declaration root, where the singer category reads them; a map left
    inside configuration is invisible to the loader and is reported so it is not silently ignored.
    """
    configuration = declaration.get("configuration", {})
    if isinstance(configuration, dict) and (
            "languages" in configuration or "defaultLanguage" in configuration):
        errors.append(f"{where}: languages and defaultLanguage belong in the declaration root, "
                      "not in configuration")
    if isinstance(configuration, dict) and "reservedPhonemes" in configuration:
        errors.append(f"{where}: reservedPhonemes belongs in the declaration root, not in "
                      "configuration")
    reserved = declaration.get("reservedPhonemes")
    if reserved is not None:
        if (not isinstance(reserved, list) or any(not isinstance(item, str) or not item
                                                  for item in reserved)):
            errors.append(f"{where}: reservedPhonemes must be an array of non-empty strings")
        elif len(set(reserved)) != len(reserved):
            errors.append(f"{where}: reservedPhonemes names a token twice")
    if "languages" not in declaration:
        return
    languages = declaration["languages"]
    if not isinstance(languages, dict):
        errors.append(f"{where}: languages must map language handles to import roles")
        return
    roles = {item.get("role") for item in declaration.get("imports", [])}
    for handle, role in languages.items():
        if not LANGUAGE_HANDLE.fullmatch(handle):
            errors.append(f"{where}: language handle {handle!r} is not an ISO 639-3 code")
        if not isinstance(role, str) or role not in roles:
            errors.append(f"{where}: language {handle} routes to role {role!r}, which does not exist")
    default = declaration.get("defaultLanguage")
    if languages and not isinstance(default, str):
        errors.append(f"{where}: a singer that declares languages must declare defaultLanguage")
    elif isinstance(default, str) and default not in languages:
        errors.append(f"{where}: defaultLanguage {default!r} is not one of its languages")


def lint_package(directory: Path, warnings: list) -> None:
    """The lint checks a packaging pass can make on its own.

    All are conventions rather than conditions, so none fails a load and none fails here.
    """
    desc = load_json(directory / "desc.json")
    contributions = desc.get("contributions", {})

    # Read the package's own inference declarations first: the linguist check below needs to know
    # what the chain it imports can do.
    inferences = {}
    for entry in contributions.get("inference", []):
        path = (directory / entry["path"]).resolve()
        if path.is_file():
            inferences[entry["id"]] = load_json(path)

    for entry in contributions.get("linguist", []):
        path = (directory / entry["path"]).resolve()
        if not path.is_file():
            continue
        declaration = load_json(path)
        language = declaration.get("language", "")
        scheme = declaration.get("scheme", "")
        where = f"{desc['id']}:{entry['id']}"

        # The identity fields are the truth and the id is a redundant copy, so a mismatch costs
        # nothing at load. It still reads as a lie to anyone scanning a package directory.
        expected = f"{language}-{scheme}"
        if entry["id"] != expected and not entry["id"].startswith(expected + "-"):
            warnings.append(
                f"{where}: id does not follow <language>-<scheme>[-<qualifier>], expected "
                f"{expected!r} or a suffixed form of it")

        # A role the language never routes to is dead weight: the executive tree only creates a
        # child for a role the conversion asks for.
        # The three fixed roles are the whole of what a linguist binds; anything else is dead
        # weight, since the executive tree only ever creates children for those.
        for item in declaration.get("imports", []):
            role = item.get("role")
            if role and role not in {"linguist/g2p", "linguist/s2p", "linguist/onset"}:
                warnings.append(f"{where}: import role {role!r} is not one this contract binds")

        # phonemes is required, so a language that cannot state a complete set has no way to be
        # honest except this bit. Checked against the chain it actually imports rather than left
        # to the author: an author who remembers is not a mechanism.
        exports = declaration.get("exports", {})
        chain = local_import(declaration, "linguist/g2p", inferences)
        if chain is not None and unbounded_chain(chain) and not exports.get("openSet"):
            warnings.append(
                f"{where}: its G2P ends by returning the original word, so the phonemes list "
                f"cannot be complete; declare openSet")

    # The same question asked of a chain's own symbols list, for the modules that declare one.
    for entry_id, declaration in inferences.items():
        if declaration.get("variant") != "pipe-chain":
            continue
        exports = declaration.get("exports", {})
        open_chain = unbounded_chain(declaration)
        if open_chain and "symbols" in exports and not exports.get("openSet"):
            warnings.append(
                f"{desc['id']}:{entry_id}: the chain ends by returning the original word, so its "
                f"symbols list cannot be complete; declare openSet")
        if not open_chain and exports.get("openSet"):
            warnings.append(
                f"{desc['id']}:{entry_id}: openSet is declared but no step can produce anything "
                f"outside the chain's own tables")

    for entry in contributions.get("singer", []):
        path = (directory / entry["path"]).resolve()
        if not path.is_file():
            continue
        declaration = load_json(path)
        # A singer routes each language handle to an import role. A role nothing routes to can
        # never be reached, which is the lint the domain contract asks for.
        # Only imports that lead to a linguist contribution are candidates: the singer's other
        # roles, such as its acoustic and vocoder models, are reached by its own provider and were
        # never meant to appear in the language map.
        languages = declaration.get("languages")
        routed = set(languages.values()) if isinstance(languages, dict) else set()
        for item in declaration.get("imports", []):
            role = item.get("role")
            ref = item.get("ref", "")
            category = ref.rsplit(":", 1)[-1].split("/", 1)[0] if ":" in ref else ""
            if category != "linguist":
                continue
            if role and role not in routed:
                warnings.append(
                    f"{desc['id']}:{entry['id']}: import role {role!r} is not referenced by any "
                    f"language, so nothing can reach it")


def four_part(version: str) -> tuple:
    parts = [int(p) for p in version.split(".")]
    return tuple((parts + [0, 0, 0, 0])[:4])


def check_dependencies(packages: list, errors: list, warnings: list) -> None:
    """Every dependency must name the target's compatibility floor, not the version it happens to be.

    The loader serves a dependency from the closed interval [compatVersion, version] of a candidate,
    so naming the floor is the widest target that is always satisfied and naming anything else
    narrows it for no gain. Two failures follow from getting this wrong, and both are silent until
    someone repackages:

      - a package whose compatVersion equals its version is compatible with nothing before it, so
        every consumer already published stops resolving the moment a new revision appears;
      - a consumer that names the target's current version instead of its floor pins itself to a
        point that a later downgrade or a partial install no longer offers.

    Reported as an error rather than a warning because after A69 there is no second mechanism: the
    floor is the only thing standing between a routine repackaging and every published voicebank
    failing to load.
    """
    known = {}
    for directory in packages:
        desc = load_json(directory / "desc.json")
        known[desc["id"]] = (desc["version"], desc.get("compatVersion", desc["version"]))

    for directory in packages:
        desc = load_json(directory / "desc.json")
        version, compat = known[desc["id"]]
        # A floor equal to the version is only honest when the package is its own first revision.
        if four_part(compat) == four_part(version) and four_part(version)[3] != 0:
            warnings.append(
                f"{desc['id']}: compatVersion equals version ({version}), which says this package "
                f"is compatible with nothing published before it. A packaging revision breaks none "
                f"of the public surfaces that would require raising the floor")

        for dependency in desc.get("dependencies", []):
            target = known.get(dependency["id"])
            if target is None:
                continue  # outside this set; guessing would be worse than staying quiet
            target_floor = target[1]
            if four_part(dependency["version"]) != four_part(target_floor):
                errors.append(
                    f"{desc['id']}: depends on {dependency['id']} {dependency['version']}, but "
                    f"that package's compatibility floor is {target_floor}. Name the floor: it is "
                    f"the widest target and the only one a later revision still satisfies")


def check_package(directory: Path, schemas: Schemas, errors: list) -> int:
    desc = load_json(directory / "desc.json")
    checked = 0
    for entry in desc.get("contributions", {}).get("singer", []):
        path = (directory / entry["path"]).resolve()
        if path.is_file():
            check_singer_languages(load_json(path),
                                   f"{desc['id']}:{entry['id']}", errors)
    for entries in desc.get("contributions", {}).values():
        for entry in entries:
            path = (directory / entry["path"]).resolve()
            if not path.is_file():
                errors.append(f"{directory.name}/{entry['id']}: {entry['path']} is missing")
                continue
            declaration = load_json(path)
            key = (declaration.get("interface"), declaration.get("level"))
            prefix = CONTRACTS.get(key)
            if prefix is None:
                continue
            where = f"{desc['id']}:{entry['id']}"
            if prefix == "linguist-1":
                check_identity(declaration, where, errors)
            validate(declaration.get("exports", {}),
                     schemas.resolve(f"{prefix}-exports.schema.json"), schemas,
                     f"{where} exports", errors)
            for i, item in enumerate(declaration.get("imports", [])):
                if "options" in item:
                    validate(item["options"],
                             schemas.resolve(f"{prefix}-import-options.schema.json"), schemas,
                             f"{where} imports[{i}].options", errors)
            checked += 1
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path,
                        help="directories holding packages, or packages themselves")
    parser.add_argument("--schemas", type=Path,
                        default=Path(__file__).resolve().parent.parent / "docs" / "schemas")
    args = parser.parse_args()

    schemas = Schemas(args.schemas)
    packages, errors, checked = [], [], 0
    for root in args.roots:
        if not root.is_dir():
            continue
        if (root / "desc.json").is_file():
            packages.append(root)
            continue
        packages += [p for p in sorted(root.iterdir()) if (p / "desc.json").is_file()]

    if not packages:
        print("no packages found", file=sys.stderr)
        return 1
    warnings = []
    for package in packages:
        checked += check_package(package, schemas, errors)
        lint_package(package, warnings)
    check_dependencies(packages, errors, warnings)

    for warning in sorted(warnings):
        print(f"warning: {warning}")
    for error in errors:
        print(f"error: {error}")
    print(f"checked {checked} declarations in {len(packages)} packages, "
          f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
