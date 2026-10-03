"""This module tests check-declarations.py, the package lint, and the definitions it shares.

The lint is the last check on a declaration before a release. These tests therefore pin the set of
packages that the lint rejects, using the test packages that scripts/make-test-fixtures.py
generates. The loader's verdict on each of those packages is recorded in
src/tests/auto/loader-verdicts.json, which test_LinguistLoad checks against the loader itself. The
lint must reject every package the loader rejects, except the packages that the record lists
together with a reason the lint cannot detect. The package shipped with this repository must pass
without errors or warnings. The tests are run with python -m unittest discover -s scripts -p
"test_*.py".
"""

import atexit
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from declarations import is_package_id, parse_version, split_ref

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parent
VERDICTS = REPOSITORY / "src" / "tests" / "auto" / "loader-verdicts.json"


def generated_fixtures() -> Path:
    """Generates the test fixture packages into a temporary directory and returns it."""
    spec = importlib.util.spec_from_file_location("make_test_fixtures",
                                                  HERE / "make-test-fixtures.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    root = Path(tempfile.mkdtemp(prefix="wolf-test-fixtures-"))
    atexit.register(shutil.rmtree, root, True)
    module.write_tree(root)
    return root


def fixture_packages() -> Path:
    """Returns the directory with the test fixture packages.

    WOLF_TEST_FIXTURES_SOURCE selects a generated directory, which the build passes through the
    ctest environment. Without it the packages are generated here, so that these tests keep working
    in a tree in which the generator has not been run.
    """
    override = os.environ.get("WOLF_TEST_FIXTURES_SOURCE")
    if override:
        root = Path(override)
        if not (root / "singer-ok" / "desc.json").is_file():
            raise SystemExit(f"WOLF_TEST_FIXTURES_SOURCE holds no fixture packages: {root}")
        return root
    return generated_fixtures()


FIXTURES = fixture_packages()
if not FIXTURES.is_dir() or not any(FIXTURES.iterdir()):
    raise SystemExit(f"the test fixture packages are missing from {FIXTURES}")

# Packages the loader accepts and the lint refuses, each for a rule the lint enforces on purpose.
STRICTER_THAN_THE_LOADER = {
    # A singer language routed to a module that is not a linguist. The loader refuses it only when a
    # linguist provider is loaded in the same transaction, which this package does not cause.
    "singer-inference-route": [],
}


def lint(*packages):
    return subprocess.run([sys.executable, str(HERE / "check-declarations.py"),
                           *[str(p) for p in packages]],
                          capture_output=True, text=True)


def package_path(name):
    """Returns the path of the named test package or package shipped with the repository."""
    # The package this repository ships is one of the generated packages, so one root answers
    # for both.
    for root in (FIXTURES,):
        if (root / name / "desc.json").is_file():
            return root / name
    raise FileNotFoundError(name)


def load_lint_module():
    spec = importlib.util.spec_from_file_location("check_declarations",
                                                  HERE / "check-declarations.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Lint(unittest.TestCase):
    def test_the_shipped_package_passes_clean(self):
        # The package this repository ships is generated with the fixtures, because the repository
        # carries the declarations of scripts/make-test-fixtures.py rather than package artifacts.
        result = lint(FIXTURES / "wolf-lang-zxx")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 error(s), 0 warning(s)", result.stdout)

    def test_a_good_singer_passes(self):
        result = lint(FIXTURES / "singer-ok")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_every_package_the_loader_refuses_is_refused(self):
        """Checks that the lint rejects every package that the loader rejects, if the reason is
        detectable from the declarations."""
        record = json.loads(VERDICTS.read_text(encoding="utf-8"))
        self.assertTrue(record["refused"])
        for name, details in record["refused"].items():
            with self.subTest(package=name):
                companions = [package_path(other) for other in details.get("lintWith", [])]
                result = lint(package_path(name), *companions)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("error:", result.stdout)

    def test_every_test_package_has_a_recorded_verdict(self):
        record = json.loads(VERDICTS.read_text(encoding="utf-8"))
        recorded = set(record["refused"]) | set(record["refusedByTheLoaderOnly"]) | set(
            record["accepted"])
        present = {path.name for path in FIXTURES.iterdir() if (path / "desc.json").is_file()}
        self.assertEqual(present, recorded)

    def test_packages_the_lint_refuses_beyond_the_loader(self):
        for name, companions in STRICTER_THAN_THE_LOADER.items():
            with self.subTest(package=name):
                result = lint(package_path(name), *map(package_path, companions))
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_dependency_must_name_the_compatibility_floor(self):
        # The loader rejects the first because no installed version satisfies it, and resolves the
        # second to an older revision. The lint rejects both because neither names the floor of
        # the provider.
        for consumer in ("compat-consumer-too-new", "compat-consumer"):
            with self.subTest(package=consumer):
                result = lint(FIXTURES / consumer, FIXTURES / "compat-provider")
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("compatibility floor", result.stdout)

    def test_an_unknown_role_is_an_error(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "roles"
            (root / "linguists" / "x").mkdir(parents=True)
            (root / "inferences" / "g").mkdir(parents=True)
            (root / "desc.json").write_text(json.dumps({
                "$version": "1.0", "id": "wolf/test-roles", "version": "1.0.0.0",
                "runtimeLevel": 1,
                "contributions": {
                    "linguist": [{"id": "zxx-passthrough", "path": "./linguists/x/linguist.json"}],
                    "inference": [{"id": "g", "path": "./inferences/g/inference.json"}],
                }}))
            (root / "inferences" / "g" / "inference.json").write_text(json.dumps({
                "interface": "org.openvpi.wolf.inference.G2P", "level": 1,
                "variant": "stub-miscount", "configuration": {}}))
            (root / "linguists" / "x" / "linguist.json").write_text(json.dumps({
                "interface": "org.openvpi.wolf.linguist.WolfLinguist", "level": 1,
                "variant": "wolf", "language": "zxx", "scheme": "passthrough",
                "exports": {"phonemes": [], "openSet": True}, "configuration": {},
                "imports": [{"role": "linguist/g2p", "ref": ":inference/g"},
                            {"role": "linguist/onsets", "ref": ":inference/g"}]}))
            result = lint(root)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("linguist/onsets", result.stdout)

    def test_a_role_must_lead_to_its_contract(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch) / "target"
            (root / "linguists" / "x").mkdir(parents=True)
            (root / "inferences" / "g").mkdir(parents=True)
            (root / "desc.json").write_text(json.dumps({
                "$version": "1.0", "id": "wolf/test-target", "version": "1.0.0.0",
                "runtimeLevel": 1,
                "contributions": {
                    "linguist": [{"id": "zxx-passthrough", "path": "./linguists/x/linguist.json"}],
                    "inference": [{"id": "g", "path": "./inferences/g/inference.json"}],
                }}))
            (root / "inferences" / "g" / "inference.json").write_text(json.dumps({
                "interface": "org.openvpi.wolf.inference.S2P", "level": 1,
                "variant": "direct", "configuration": {}}))
            (root / "linguists" / "x" / "linguist.json").write_text(json.dumps({
                "interface": "org.openvpi.wolf.linguist.WolfLinguist", "level": 1,
                "variant": "wolf", "language": "zxx", "scheme": "passthrough",
                "exports": {"phonemes": []}, "configuration": {},
                "imports": [{"role": "linguist/g2p", "ref": ":inference/g"}]}))
            result = lint(root)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("org.openvpi.wolf.inference.G2P", result.stdout)

    def test_a_malformed_declaration_is_reported_not_raised(self):
        with tempfile.TemporaryDirectory() as scratch:
            broken = Path(scratch) / "broken"
            broken.mkdir()
            (broken / "desc.json").write_text('{"id": ')
            missing = Path(scratch) / "missing"
            missing.mkdir()
            (missing / "desc.json").write_text(json.dumps({
                "version": "1.a", "contributions": {"linguist": [{"id": "x"}]}}))
            result = lint(Path(scratch))
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("Traceback", result.stderr)
            self.assertIn("broken", result.stdout)
            self.assertIn("'1.a'", result.stdout)


class SharedDefinitions(unittest.TestCase):
    def test_versions_follow_the_loader(self):
        # The cases of readVersion() in synthrt's PackageLoader.cpp.
        self.assertEqual(parse_version("1"), (1, 0, 0, 0))
        self.assertEqual(parse_version("1.0.2.10"), (1, 0, 2, 10))
        self.assertEqual(parse_version("0.0.0.0"), (0, 0, 0, 0))
        for bad in ["", "1.0.0.0.0", "01.0", "1.00", "1..0", "1.0.", ".1", "1.a", "-1", " 1", None,
                    1]:
            with self.subTest(version=bad):
                self.assertIsNone(parse_version(bad))

    def test_package_ids_follow_the_loader(self):
        for good in ["wolf/lang-cmn", "vendor", "a_b/C-9/x"]:
            self.assertTrue(is_package_id(good), good)
        for bad in ["", "wolf//x", "/wolf", "wolf/", "wolf/lang cmn", "wolf.lang", None]:
            with self.subTest(package_id=bad):
                self.assertFalse(is_package_id(bad))

    def test_refs_split_as_the_loader_splits_them(self):
        self.assertEqual(split_ref(":inference/g2p"), ("", "inference", "g2p"))
        self.assertEqual(split_ref("wolf/lang-cmn:linguist/cmn"), ("wolf/lang-cmn", "linguist",
                                                                   "cmn"))
        for bad in ["inference/g2p", "a:b:c/d", ":inference", ":inference/a/b", None]:
            self.assertIsNone(split_ref(bad), bad)

    def test_the_identity_grammar_is_the_schema_and_the_loader(self):
        """Checks the patterns that the lint reads from the published schema against the cases of
        the loader's own test (test_ManifestValues_IdentityGrammars), plus a trailing newline,
        which a Python `$` accepts."""
        module = load_lint_module()
        grammar = module.IdentityGrammar(module.Schemas(REPOSITORY / "docs" / "schemas"))
        for good in ["cmn", "qaa"]:
            self.assertTrue(grammar.is_language(good), good)
        for bad in ["cm", "cmns", "Cmn", "cmn\n", "", None]:
            self.assertFalse(grammar.is_language(bad), repr(bad))
        for good in ["pinyin", "xsampa-geminate", "ds2"]:
            self.assertTrue(grammar.is_scheme(good), good)
        for bad in ["", "-pinyin", "pinyin-", "pin--yin", "pin_yin", "pinyin\n"]:
            self.assertFalse(grammar.is_scheme(bad), repr(bad))


if __name__ == "__main__":
    unittest.main()
