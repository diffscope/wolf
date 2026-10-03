"""Tests for scripts/make-test-fixtures.py and for the record of the loader's verdicts.

The tests are run with python -m unittest discover -s scripts -p "test_*.py".
"""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parent
GENERATOR = HERE / "make-test-fixtures.py"
VERDICTS = REPOSITORY / "src" / "tests" / "auto" / "loader-verdicts.json"

# The groups of loader verdicts that record one package each, in the order the file documents them.
VERDICT_GROUPS = ("refused", "refusedByTheLoaderOnly", "accepted")


def load_generator():
    spec = importlib.util.spec_from_file_location("make_test_fixtures", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FixturePackages(unittest.TestCase):
    def test_the_table_holds_a_manifest_for_every_package(self):
        module = load_generator()
        self.assertTrue(module.PACKAGES, "the fixture table is empty")
        for package, files in module.PACKAGES.items():
            with self.subTest(package=package):
                self.assertIn("desc.json", files)
                self.assertNotIn("/", package, "a package is a top level directory")

    def test_generation_is_deterministic(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            module.write_tree(Path(first))
            module.write_tree(Path(second))
            left = {path.relative_to(first).as_posix(): path.read_bytes()
                    for path in Path(first).rglob("*") if path.is_file()}
            right = {path.relative_to(second).as_posix(): path.read_bytes()
                     for path in Path(second).rglob("*") if path.is_file()}
            self.assertEqual(left, right)
            self.assertEqual([], module.compare(Path(first)))

    def test_check_reports_a_changed_file(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as root:
            module.write_tree(Path(root))
            victim = Path(root) / "pinyin-engine" / "desc.json"
            victim.write_bytes(victim.read_bytes() + b"\n")
            differences = module.compare(Path(root))
            self.assertTrue(any(line.startswith("differs:") for line in differences), differences)

    def test_check_reports_an_extra_package(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as root:
            module.write_tree(Path(root))
            extra = Path(root) / "unexpected-package"
            extra.mkdir()
            (extra / "desc.json").write_bytes(b"{}")
            differences = module.compare(Path(root))
            self.assertTrue(any(line.startswith("unexpected:") for line in differences), differences)

    def test_check_reports_a_missing_file(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as root:
            module.write_tree(Path(root))
            (Path(root) / "singer-ok" / "desc.json").unlink()
            differences = module.compare(Path(root))
            self.assertTrue(any(line.startswith("missing:") for line in differences), differences)

    def test_the_generated_files_use_line_feeds(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as root:
            module.write_tree(Path(root))
            carriage_returns = sum(path.read_bytes().count(b"\r\n")
                                   for path in Path(root).rglob("*") if path.is_file())
            self.assertEqual(0, carriage_returns, "the generated files must not contain CRLF")

    def test_list_matches_the_table(self):
        module = load_generator()
        done = subprocess.run([sys.executable, str(GENERATOR), "--list"],
                              capture_output=True, check=True, text=True)
        self.assertEqual(done.stdout.split(), sorted(module.PACKAGES))

    def test_the_recorded_verdicts_cover_every_package(self):
        """Every generated package has exactly one verdict, and no verdict is stale."""
        module = load_generator()
        verdicts = json.loads(VERDICTS.read_text(encoding="utf-8"))
        recorded: set[str] = set()
        for group in VERDICT_GROUPS:
            recorded |= set(verdicts[group])
        self.assertEqual(recorded, set(module.PACKAGES),
                         f"verdicts and packages differ: {sorted(recorded ^ set(module.PACKAGES))}")


if __name__ == "__main__":
    unittest.main()
