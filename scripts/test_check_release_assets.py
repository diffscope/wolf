"""Unit tests for scripts/check-release-assets.py.

The checker guards against a release that updates one copy of the package facts
and forgets another, so every test breaks one copy on purpose and asserts that
the checker reports it.
"""

import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "check_release_assets", REPOSITORY / "scripts/check-release-assets.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)

ASSETS = """\
set(WOLF_LANG_PACKAGES_BUNDLE_VERSION "1.2.3.4")
set(WOLF_LANG_PACKAGES_SUITES "multi;cmn;zxx")

set(WOLF_LANG_MULTI_FILE "wolf-g2p-multi-1.0.0.4.zip")
set(WOLF_LANG_MULTI_SHA512 "aa")
set(WOLF_LANG_MULTI_DIR "wolf-g2p-multi")
set(WOLF_LANG_CMN_FILE "wolf-lang-cmn-1.0.1.4.zip")
set(WOLF_LANG_CMN_SHA512 "bb")
set(WOLF_LANG_CMN_DIR "wolf-lang-cmn")
set(WOLF_LANG_ZXX_FILE "wolf-lang-zxx-1.0.0.0.zip")
set(WOLF_LANG_ZXX_SHA512 "cc")
set(WOLF_LANG_ZXX_DIR "wolf-lang-zxx")
"""

PORT = {
    "name": "wolf-lang-packages",
    "version-string": "1.2.3.4",
    "default-features": ["cmn", "zxx"],
    "features": {
        "cmn": {"dependencies": [{"name": "wolf-lang-packages", "features": ["multi"]}]},
        "multi": {"description": "shared model"},
        "zxx": {"description": "no resources"},
    },
}


class CheckerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.port = Path(self.temporary.name) / "port"
        self.port.mkdir()
        self.write_assets(ASSETS)
        self.write_port(PORT)

    def tearDown(self):
        self.temporary.cleanup()

    def write_assets(self, text):
        (self.port / "assets.cmake").write_text(text, encoding="utf-8")

    def write_port(self, document):
        (self.port / "vcpkg.json").write_text(json.dumps(document), encoding="utf-8")

    def write_manifest(self, version="1.2.3.4"):
        path = Path(self.temporary.name) / "manifest.json"
        path.write_text(json.dumps({
            "bundleVersion": version,
            "packages": [
                {"file": "wolf-g2p-multi-1.0.0.4.zip", "sha512": "aa"},
                {"file": "wolf-lang-cmn-1.0.1.4.zip", "sha512": "bb"},
                {"file": "wolf-lang-zxx-1.0.0.0.zip", "sha512": "cc"},
            ],
        }), encoding="utf-8")
        return path

    def run_checker(self, manifest=None, dist=None):
        arguments = ["--port", str(self.port)]
        arguments += ["--manifest", str(manifest or (Path(self.temporary.name) / "missing.json"))]
        if dist:
            arguments += ["--dist", str(dist)]
        return checker.main(arguments)

    def test_consistent_copies_pass(self):
        self.assertEqual(self.run_checker(self.write_manifest()), 0)

    def test_bundle_version_drift_fails(self):
        self.write_port(dict(PORT, **{"version-string": "9.9.9.9"}))
        self.assertNotEqual(self.run_checker(), 0)

    def test_feature_drift_fails(self):
        self.write_port(dict(PORT, features=dict(PORT["features"], kor={"description": "x"})))
        self.assertNotEqual(self.run_checker(), 0)

    def test_default_feature_outside_features_fails(self):
        self.write_port(dict(PORT, **{"default-features": ["cmn", "nope"]}))
        self.assertNotEqual(self.run_checker(), 0)

    def test_suite_without_archive_fails(self):
        self.write_assets(ASSETS.replace('set(WOLF_LANG_CMN_FILE "wolf-lang-cmn-1.0.1.4.zip")\n', ""))
        self.assertNotEqual(self.run_checker(), 0)

    def test_release_manifest_version_drift_fails(self):
        self.assertNotEqual(self.run_checker(self.write_manifest(version="0.0.0.1")), 0)

    def test_release_manifest_pin_drift_fails(self):
        path = self.write_manifest()
        document = json.loads(path.read_text(encoding="utf-8"))
        document["packages"][0]["sha512"] = "not-the-pin"
        path.write_text(json.dumps(document), encoding="utf-8")
        self.assertNotEqual(self.run_checker(path), 0)

    def test_absent_manifest_passes_but_reports_the_skip(self):
        # A clean checkout has no release manifest. The port checks still run, and the
        # unverified pins must be reported rather than passed silently.
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(self.run_checker(), 0)
        self.assertIn("skipped", output.getvalue())
        self.assertIn("unverified", output.getvalue())

    def test_archive_bytes_are_checked_when_present(self):
        manifest = self.write_manifest()
        dist = manifest.parent
        for name, digest in (("wolf-g2p-multi-1.0.0.4.zip", "aa"),
                             ("wolf-lang-cmn-1.0.1.4.zip", "bb"),
                             ("wolf-lang-zxx-1.0.0.0.zip", "cc")):
            (dist / name).write_bytes(b"payload")
        # The stubs above do not match the real SHA512 of the payload, so the archive
        # check must fail instead of trusting the names.
        self.assertNotEqual(self.run_checker(manifest, dist=dist), 0)


if __name__ == "__main__":
    unittest.main()
