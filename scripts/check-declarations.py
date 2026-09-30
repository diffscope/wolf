#!/usr/bin/env python3
"""Validates module declarations against the published contract schemas and runs local lint checks.

The schemas in docs/schemas form the machine-readable part of each interface contract, as defined
by the specification. Publishing them is useful only if they agree with the loader; this script
therefore reads real packages and validates the exports and import options of every declaration
against them.

The script also runs two checks from the lint list of the domain contract, namely the two that
require only the declarations. The other two checks in that list compare declarations against the
phoneme tables of a voicebank or a model. They belong to the editor at run time and are outside the
scope of a packaging pass.

Each finding is an error or a warning. Errors correspond to conditions the loader rejects: schema
violations, missing identity fields and roles, invalid package ids and versions, a malformed singer
language map, a linguist role whose target implements the wrong interface, a G2P or S2P member that
does not declare the language pair of its linguist (domain contract §5.3), and a singer language
whose target is not a linguist of that language. Checks that require the target of an import run
only if the target package is among the packages given on the command line, and are skipped
otherwise, because an unresolved target cannot be checked without guessing. Warnings report
conventions the loader does not enforce, and declarations of contracts for which this script has no
schema.

Two rules are reported as errors although the loader accepts the package. The first is the
dependency floor (check_dependencies): a dependency on any version other than the compatibility
floor of its target resolves at present but fails to resolve after the next revision of the target,
so the lint rejects it before publication. The second is a singer language routed to a module that
is not a linguist: the loader rejects it only if a linguist provider is loaded in the same
transaction, and accepts it otherwise.

An unreadable declaration, or a declaration without a field the script reads, is reported as an
error naming the file instead of terminating the run with a traceback.

The validator supports exactly the keywords those schemas use: type, properties, required,
additionalProperties, items, uniqueItems, minLength, pattern, oneOf and $ref, plus the annotation
keywords $id, $schema, title, description and default. It is not a general JSON Schema
implementation. A schema that uses any other keyword terminates the run instead of passing
unchecked.
"""

import argparse
import sys
from pathlib import Path

from declarations import (INFERENCE_CATEGORY, LINGUIST_CATEGORY, LINGUIST_INTERFACE,
                          LINGUIST_ROLES, ROLE_G2P, ROLE_INTERFACES, ROLE_ONSET, ROLE_S2P,
                          SINGER_CATEGORY,
                          is_package_id, load_json, parse_version, schema_pattern, split_ref)


# A keyword not listed here terminates the run instead of being skipped, because an unimplemented
# keyword would otherwise appear as a rule that passed. The annotation keywords, including
# "default", need no implementation because JSON Schema defines them without assertions.
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
        # Python's bool is a subclass of int. A boolean therefore matches only the boolean type,
        # and the boolean type matches only a boolean.
        if isinstance(value, bool) != (schema["type"] == "boolean"):
            errors.append(f"{where}: expected {schema['type']}")
            return
        if not isinstance(value, expected):
            errors.append(f"{where}: expected {schema['type']}")
            return

    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{where}: must not be empty")
        if "pattern" in schema and not schema_pattern(schema["pattern"]).search(value):
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


# The schema pair of each contract, keyed by (interface, level). A declaration whose contract is
# not listed is not validated against a schema.
CONTRACTS = {
    (LINGUIST_INTERFACE, 1): "linguist-1",
    (ROLE_INTERFACES[ROLE_G2P], 1): "g2p-1",
    (ROLE_INTERFACES[ROLE_S2P], 1): "s2p-1",
    (ROLE_INTERFACES[ROLE_ONSET], 1): "onset-1",
}


class PackageSet:
    """Indexes the packages given on the command line so that import refs can be resolved.

    A ref names a package without a version, and the lint holds one copy of each package id. A ref
    into another package therefore resolves only if that package is also given. Any other ref
    remains unresolved, and the checks that require its target are skipped.
    """

    def __init__(self):
        self.by_id = {}

    def add(self, directory: Path) -> None:
        desc = load_json(directory / "desc.json")
        package_id = desc.get("id")
        if isinstance(package_id, str):
            self.by_id[package_id] = directory

    def resolve(self, ref, referrer: Path):
        """Returns the declaration that a ref names, or None if the ref is malformed or the
        declaration is not readable from the given packages."""
        parts = split_ref(ref)
        if parts is None:
            return None
        package_id, category, contribution = parts
        directory = referrer if not package_id else self.by_id.get(package_id)
        if directory is None:
            return None
        desc = load_json(directory / "desc.json")
        for entry in desc.get("contributions", {}).get(category, []):
            if isinstance(entry, dict) and entry.get("id") == contribution:
                path = (directory / entry.get("path", "")).resolve()
                if path.is_file():
                    return load_json(path)
        return None


def unbounded_chain(declaration: dict) -> bool:
    """Returns whether a chain can produce output outside every list it declares.

    A single step suffices: a fallback step that returns the word unchanged. The output then
    includes arbitrary user input, and no list of symbols or phonemes can be complete.
    """
    if declaration.get("variant") != "pipe-chain":
        return False
    # The loader defaults useOriginal to true, so a fallback step without the parameter returns
    # the word unchanged.
    return any(step.get("step") == "fallback" and step.get("params", {}).get("useOriginal", True)
               for step in declaration.get("configuration", {}).get("steps", []))


def local_import(declaration: dict, role: str, inferences: dict):
    """Returns the declaration that a role refers to if the ref points into this package, or None
    otherwise.

    A ref into another package is not readable here and is not resolved, because resolving it
    would require guessing.
    """
    for item in declaration.get("imports", []):
        if item.get("role") != role:
            continue
        ref = item.get("ref", "")
        prefix = f":{INFERENCE_CATEGORY}/"
        if not isinstance(ref, str) or not ref.startswith(prefix):
            return None
        return inferences.get(ref[len(prefix):])
    return None


class IdentityGrammar:
    """Holds the grammars of language handles and scheme names, read from the published schema.

    docs/schemas/language-scheme.schema.json specifies both grammars, and the loader implements the
    same grammars in isLanguageHandle() and isSchemeName() (src/lib/Support/ManifestValues.cpp).
    Reading the patterns from the schema avoids a separate copy in this script that could diverge.
    """

    def __init__(self, schemas):
        properties = schemas.resolve("language-scheme.schema.json")["properties"]
        self.language_pattern = properties["language"]["pattern"]
        self.scheme_pattern = properties["scheme"]["pattern"]
        self._language = schema_pattern(self.language_pattern)
        self._scheme = schema_pattern(self.scheme_pattern)

    def is_language(self, value) -> bool:
        return isinstance(value, str) and bool(self._language.search(value))

    def is_scheme(self, value) -> bool:
        return isinstance(value, str) and bool(self._scheme.search(value))


def imports_of(declaration: dict) -> list:
    """Returns the import entries of a declaration that are objects, ignoring entries of other
    types."""
    imports = declaration.get("imports", [])
    if not isinstance(imports, list):
        return []
    return [item for item in imports if isinstance(item, dict)]


def check_identity(declaration: dict, grammar: IdentityGrammar, where: str, errors: list) -> None:
    """Checks the two identity fields that the linguist category reads, using the grammar the
    loader applies, and the roles that the provider binds."""
    language = declaration.get("language")
    scheme = declaration.get("scheme")
    if not grammar.is_language(language):
        errors.append(f"{where}: language must be an ISO 639-3 code of three lowercase letters")
    if not grammar.is_scheme(scheme):
        errors.append(f"{where}: scheme must match {grammar.scheme_pattern}")
    roles = [item.get("role") for item in imports_of(declaration)]
    if ROLE_G2P not in roles:
        errors.append(f"{where}: a linguist must import a {ROLE_G2P} role")
    # A linguist binds only the three fixed roles. The loader ignores any other role, and the
    # composition is then shallower than the declaration suggests; a misspelt role such as
    # linguist/onsets is therefore an error instead of a warning.
    for role in roles:
        if role not in LINGUIST_ROLES:
            errors.append(f"{where}: import role {role!r} is not bound by this contract; the "
                          f"bound roles are {', '.join(LINGUIST_ROLES)}")


def check_linguist_targets(declaration: dict, directory: Path, packages: PackageSet, where: str,
                           errors: list) -> None:
    """Applies the checks of the loader's import validator to each member of a linguist composition.

    The target of each role must implement the contract that the role names, and a G2P or S2P
    member that declares exports.languages must include the language pair of the linguist (domain
    contract §5.3). A member without exports.languages is exempt from the static match. Mirrors
    validateLinguistRole() and validateLanguageMatch() in the wolf provider.
    """
    pair = {"language": declaration.get("language"), "scheme": declaration.get("scheme")}
    for item in imports_of(declaration):
        role = item.get("role")
        expected = ROLE_INTERFACES.get(role)
        if expected is None:
            continue
        target = packages.resolve(item.get("ref"), directory)
        if not isinstance(target, dict):
            continue
        interface = target.get("interface")
        if interface != expected:
            errors.append(f"{where}: role {role} leads to {item.get('ref')}, whose interface is "
                          f"{interface!r} rather than {expected}")
            continue
        if role not in (ROLE_G2P, ROLE_S2P):
            continue
        exports = target.get("exports", {})
        declared = exports.get("languages") if isinstance(exports, dict) else None
        if not isinstance(declared, list) or not declared:
            continue
        if pair not in declared:
            errors.append(f"{where}: the {role} target {item.get('ref')} does not declare "
                          f"{pair['language']}/{pair['scheme']} among its exports languages")


def check_singer_languages(declaration: dict, grammar: IdentityGrammar, where: str,
                           errors: list) -> None:
    """Checks the shape that the loader requires of a singer's language map, if the declaration
    has a language map.

    Both fields belong in the declaration root, where the singer category reads them. The loader
    ignores a map inside configuration, so the lint reports such a map.
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
            errors.append(f"{where}: reservedPhonemes contains a duplicate token")
    if "languages" not in declaration:
        return
    languages = declaration["languages"]
    if not isinstance(languages, dict):
        errors.append(f"{where}: languages must map language handles to import roles")
        return
    roles = {item.get("role") for item in imports_of(declaration)}
    for handle, role in languages.items():
        if not grammar.is_language(handle):
            errors.append(f"{where}: language handle {handle!r} is not an ISO 639-3 code")
        if not isinstance(role, str) or role not in roles:
            errors.append(f"{where}: language {handle} routes to role {role!r}, which does not exist")
    default = declaration.get("defaultLanguage")
    if languages and not isinstance(default, str):
        errors.append(f"{where}: a singer that declares languages must declare defaultLanguage")
    elif isinstance(default, str) and default not in languages:
        errors.append(f"{where}: defaultLanguage {default!r} is not among the declared languages")


def check_singer_targets(declaration: dict, directory: Path, packages: PackageSet, where: str,
                         errors: list) -> None:
    """Checks that every language of a singer leads to a linguist contribution of that language.

    Mirrors validateSingerLanguage() in the wolf provider. The loader applies this check only if a
    linguist provider exists in the transaction, so a singer whose languages all lead to other
    categories loads if no linguist provider is present; the lint rejects such a singer in either
    case. The language of the target is compared if the target is readable.
    """
    languages = declaration.get("languages")
    if not isinstance(languages, dict):
        return
    refs = {item.get("role"): item.get("ref") for item in imports_of(declaration)}
    for handle, role in languages.items():
        ref = refs.get(role)
        parts = split_ref(ref)
        if parts is None:
            continue
        if parts[1] != LINGUIST_CATEGORY:
            errors.append(f"{where}: language {handle} routes to role {role}, which leads to "
                          f"{ref}, not to a linguist contribution")
            continue
        target = packages.resolve(ref, directory)
        if not isinstance(target, dict):
            continue
        if target.get("interface") != LINGUIST_INTERFACE:
            errors.append(f"{where}: language {handle} routes to {ref}, whose interface is "
                          f"{target.get('interface')!r} rather than {LINGUIST_INTERFACE}")
        elif target.get("language") != handle:
            errors.append(f"{where}: language {handle} routes to {ref}, a linguist whose language "
                          f"is {target.get('language')!r}")


def check_desc(desc: dict, where: str, errors: list) -> None:
    """Checks the package identity fields using the grammar the loader applies."""
    if not is_package_id(desc.get("id")):
        errors.append(f"{where}: id {desc.get('id')!r} must be segments of letters, digits, _ "
                      f"and - separated by /")
    for field in ("version", "compatVersion"):
        if field == "compatVersion" and field not in desc:
            continue
        if parse_version(desc.get(field)) is None:
            errors.append(f"{where}: {field} {desc.get(field)!r} must be one to four dot "
                          f"separated decimal numbers without leading zeros")
    for dependency in desc.get("dependencies", []):
        if not isinstance(dependency, dict):
            errors.append(f"{where}: a dependency must be an object")
            continue
        if not is_package_id(dependency.get("id")):
            errors.append(f"{where}: dependency id {dependency.get('id')!r} is not a package id")
        if parse_version(dependency.get("version")) is None:
            errors.append(f"{where}: dependency {dependency.get('id')} version "
                          f"{dependency.get('version')!r} must be one to four dot separated "
                          f"decimal numbers without leading zeros")


def lint_package(directory: Path, warnings: list) -> None:
    """Runs the lint checks that require only the declarations of one package.

    All of these checks report conventions rather than load conditions. None of them causes a load
    failure, and each finding is reported as a warning.
    """
    desc = load_json(directory / "desc.json")
    contributions = desc.get("contributions", {})

    # Read the package's inference declarations first, because the linguist check below inspects
    # the chain that each linguist imports.
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

        # The identity fields are authoritative and the id is a redundant copy, so a mismatch has
        # no effect at load time. The mismatch is reported because it misleads readers of the
        # package directory.
        expected = f"{language}-{scheme}"
        if entry["id"] != expected and not entry["id"].startswith(expected + "-"):
            warnings.append(
                f"{where}: id does not follow <language>-<scheme>[-<qualifier>], expected "
                f"{expected!r} or a suffixed form of it")

        # exports.phonemes is required, so openSet is the only means for a language without a
        # complete phoneme set to declare that fact. The check inspects the chain that the
        # linguist imports instead of relying on the author.
        exports = declaration.get("exports", {})
        chain = local_import(declaration, ROLE_G2P, inferences)
        if chain is not None and unbounded_chain(chain) and not exports.get("openSet"):
            warnings.append(
                f"{where}: its G2P ends by returning the original word, so the phonemes list "
                f"cannot be complete; declare openSet")

    # Apply the same check to the symbols list of each chain that declares a symbols list.
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
                f"{desc['id']}:{entry_id}: openSet is declared, but no step can produce output "
                f"outside the chain's tables")

    for entry in contributions.get("singer", []):
        path = (directory / entry["path"]).resolve()
        if not path.is_file():
            continue
        declaration = load_json(path)
        # A singer routes each language handle to an import role. A role that no language routes
        # to is unreachable, and the domain contract requires the lint to report it. Only imports
        # that lead to a linguist contribution are considered: the other roles of the singer, such
        # as its acoustic and vocoder models, are bound by the singer provider and do not appear
        # in the language map.
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
                    f"language and is therefore unreachable")


def check_dependencies(packages: list, errors: list, warnings: list) -> None:
    """Requires every dependency to name the compatibility floor of its target instead of the
    target's current version.

    The loader satisfies a dependency from the closed interval [compatVersion, version] of a
    candidate. The floor is therefore the widest requirement that is always satisfied, and any other
    version narrows the requirement without benefit. A violation causes one of two failures, both
    undetected until the next repackaging:

      - a package whose compatVersion equals its version declares no compatibility with earlier
        revisions, so every published consumer fails to resolve once a new revision is installed;
      - a consumer that names the current version of its target instead of the floor requires a
        version that a later downgrade or a partial installation no longer provides.

    The rule is reported as an error instead of a warning because, since A69, no other mechanism
    exists: the floor alone prevents a routine repackaging from causing every published voicebank
    to fail to load.
    """
    # check_desc already reports ids and versions that the loader rejects. A package with such an
    # id or version is excluded from the comparisons below, which would only repeat that finding
    # less clearly.
    known = {}
    for directory in packages:
        desc = load_json(directory / "desc.json")
        version = desc.get("version")
        compat = desc.get("compatVersion", version)
        if (not is_package_id(desc.get("id")) or parse_version(version) is None
                or parse_version(compat) is None):
            continue
        known[desc["id"]] = (version, compat)

    for directory in packages:
        desc = load_json(directory / "desc.json")
        if desc.get("id") not in known:
            continue
        version, compat = known[desc["id"]]
        # A floor equal to the version is correct only for a first release, whose fourth version
        # component is 0.
        if parse_version(compat) == parse_version(version) and parse_version(version)[3] != 0:
            warnings.append(
                f"{desc['id']}: compatVersion equals version ({version}), which declares this "
                f"package incompatible with every earlier revision. A packaging revision changes "
                f"none of the public surfaces that would require raising the floor")

        for dependency in desc.get("dependencies", []):
            if not isinstance(dependency, dict):
                continue
            target = known.get(dependency.get("id"))
            if target is None:
                continue  # target not among the given packages; not checked
            target_floor = target[1]
            requested = parse_version(dependency.get("version"))
            if requested is not None and requested != parse_version(target_floor):
                errors.append(
                    f"{desc['id']}: depends on {dependency['id']} {dependency['version']}, but "
                    f"that package's compatibility floor is {target_floor}. A dependency must name "
                    f"the floor, because the floor is the widest requirement and the only version "
                    f"that later revisions still satisfy")


def check_package(directory: Path, schemas: Schemas, grammar: IdentityGrammar,
                  packages: PackageSet, errors: list, warnings: list) -> int:
    desc = load_json(directory / "desc.json")
    check_desc(desc, str(desc.get("id", directory.name)), errors)
    checked = 0
    for entry in desc.get("contributions", {}).get(SINGER_CATEGORY, []):
        path = (directory / entry["path"]).resolve()
        if path.is_file():
            declaration = load_json(path)
            where = f"{desc['id']}:{entry['id']}"
            check_singer_languages(declaration, grammar, where, errors)
            check_singer_targets(declaration, directory, packages, where, errors)
    for category, entries in desc.get("contributions", {}).items():
        for entry in entries:
            path = (directory / entry["path"]).resolve()
            if not path.is_file():
                errors.append(f"{directory.name}/{entry['id']}: {entry['path']} is missing")
                continue
            declaration = load_json(path)
            key = (declaration.get("interface"), declaration.get("level"))
            prefix = CONTRACTS.get(key)
            where = f"{desc['id']}:{entry['id']}"
            if prefix is None:
                # The loader interprets such a declaration through the provider registered for its
                # triple, and this lint has no schema for it. The declaration is reported instead of
                # passing silently, so that a misspelt interface is detected before load.
                if category in (LINGUIST_CATEGORY, INFERENCE_CATEGORY):
                    warnings.append(f"{where}: interface {key[0]!r} Level {key[1]!r} has no "
                                    f"schema in this lint; its exports and import options are "
                                    f"not checked")
                continue
            if prefix == "linguist-1":
                check_identity(declaration, grammar, where, errors)
                check_linguist_targets(declaration, directory, packages, where, errors)
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
                        default=Path(__file__).resolve().parent.parent / "docs" / "schemas",
                        help="directory of the contract schemas (default: docs/schemas)")
    args = parser.parse_args()

    schemas = Schemas(args.schemas)
    grammar = IdentityGrammar(schemas)
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
    # An unreadable declaration, or a declaration without a field the script reads, is reported as
    # an error against its package, and the remaining packages are still checked.
    readable = []
    index = PackageSet()
    for package in packages:
        try:
            index.add(package)
            readable.append(package)
        except (OSError, ValueError) as error:
            errors.append(f"{package.name}: {error}")
    for package in readable:
        try:
            checked += check_package(package, schemas, grammar, index, errors, warnings)
            lint_package(package, warnings)
        except (OSError, ValueError) as error:
            errors.append(f"{package.name}: {error}")
        except (KeyError, TypeError, AttributeError) as error:
            errors.append(f"{package.name}: a declaration is malformed at a field the lint reads "
                          f"({type(error).__name__}: {error})")
    try:
        check_dependencies(readable, errors, warnings)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        errors.append(f"dependency check: a declaration is malformed ({type(error).__name__}: "
                      f"{error})")

    for warning in sorted(warnings):
        print(f"warning: {warning}")
    for error in errors:
        print(f"error: {error}")
    print(f"checked {checked} declarations in {len(packages)} packages, "
          f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
