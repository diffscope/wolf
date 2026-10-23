#!/usr/bin/env python3
"""Keeps an already published voicebank loadable.

`make-voicebank-fixture.py` generates the voicebank fixture from the converted packages and fills
each dependency with the target package's `compatVersion`. That keeps the fixture loadable by
construction, which also makes it blind to the failure that matters here: the loader accepts a
dependency only while `compatVersion <= named version <= version` holds, so a package that raises
its floor above the version an already published voicebank names stops resolving that voicebank. A
fixture that follows the floor can never sit on the receiving end of that.

`scripts/fixtures/voicebank-pinned` is the other direction: a hand written snapshot of the versions
that were published, which is never regenerated. This module compares it against the packages that
are installed now. While every dependency still covers its pinned version, the old voicebank keeps
loading. The moment a floor moves past it, this check fails, which is the point: raising a floor is
allowed, but it has to be a decision with a migration story rather than a side effect of a rebuild.

Set `WOLF_LANG_PACKAGES_ROOT` to a directory holding the packages to compare against. Without it the
comparison skips, so the file stays runnable on a checkout that installed nothing.
"""

import json
import os
import unittest
from pathlib import Path

from declarations import parse_version

REPO_ROOT = Path(__file__).resolve().parent.parent
FIXTURE = REPO_ROOT / "scripts" / "fixtures" / "voicebank-pinned"
PACKAGES_ENV = "WOLF_LANG_PACKAGES_ROOT"


def read_fixture() -> dict:
    return json.loads((FIXTURE / "desc.json").read_text(encoding="utf-8"))


def installed_packages():
    """Returns {id: desc} for the packages under the root named by the environment, or None.

    Without the environment variable the tree that the port installs is tried, so that CI checks the
    pinned versions against the release it just installed without having to pass anything. A
    checkout that installed nothing returns None and the comparison skips.
    """
    root = os.environ.get(PACKAGES_ENV)
    if root:
        base = Path(root)
        if not base.is_dir():
            raise AssertionError(f"{PACKAGES_ENV}={root} is not a directory")
    else:
        base = next(iter(sorted((REPO_ROOT / "build" / "vcpkg_installed").glob(
            "*/share/wolf/packages"))), None)
        if base is None:
            return None
    packages = {}
    for directory in sorted(base.iterdir()):
        desc = directory / "desc.json"
        if not desc.is_file():
            continue
        document = json.loads(desc.read_text(encoding="utf-8"))
        packages[document.get("id")] = document
    if not packages:
        raise AssertionError(f"{PACKAGES_ENV}={root} holds no packages")
    return packages


class PinnedVoicebankTest(unittest.TestCase):
    def test_the_snapshot_is_a_package_the_loader_accepts(self):
        desc = read_fixture()
        self.assertTrue((FIXTURE / "singers" / "zh" / "singer.json").is_file(),
                        "the snapshot has no singer, so it is not a voicebank")
        dependencies = desc.get("dependencies", [])
        self.assertTrue(dependencies, "the snapshot has no dependencies, so it guards nothing")
        seen = set()
        for dependency in dependencies:
            identifier = dependency.get("id")
            self.assertTrue(identifier, "a dependency carries no id")
            self.assertNotEqual(identifier, desc.get("id"),
                                "a package may not depend on itself")
            self.assertNotIn(identifier, seen, f"{identifier} is listed twice")
            seen.add(identifier)
            self.assertIsNotNone(parse_version(dependency.get("version", "")),
                                 f"{identifier} does not name a version the loader accepts")

    def test_every_pinned_version_is_still_covered(self):
        packages = installed_packages()
        if packages is None:
            self.skipTest(f"set {PACKAGES_ENV} to the installed packages to compare against them")
        for dependency in read_fixture()["dependencies"]:
            identifier = dependency["id"]
            pinned = parse_version(dependency["version"])
            package = packages.get(identifier)
            self.assertIsNotNone(package, f"{identifier} is not installed at all")
            floor = parse_version(package.get("compatVersion", package["version"]))
            version = parse_version(package["version"])
            self.assertLessEqual(
                floor, pinned,
                f"{identifier} moved its compatibility floor to {package.get('compatVersion')}, "
                f"past the {dependency['version']} that published voicebanks name. The voicebank "
                f"will not load. Rebuilding a package must not do this on its own: keep the floor, "
                f"or ship a migration for the voicebanks that are already out.")
            self.assertGreaterEqual(
                version, pinned,
                f"{identifier} is installed at {package['version']}, below the "
                f"{dependency['version']} that published voicebanks name.")


if __name__ == "__main__":
    unittest.main()
