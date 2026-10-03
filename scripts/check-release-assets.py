#!/usr/bin/env python3
"""Checks that the copies of the language package release facts agree.

The same release facts are written in more than one place: the generated
``assets.cmake`` inside the vcpkg port, the port manifest that vcpkg reads, and the
release manifest that ``make-lang-release.py`` writes next to the archives. A
release that updates one copy and forgets another yields a port that downloads
the wrong bytes or a vcpkg feature that names a package nobody published, so this
script compares the copies offline.

Usage:

    python3 scripts/check-release-assets.py [--port DIR] [--manifest FILE] [--dist DIR]

The release manifest lives in the gitignored build tree, so a clean checkout does
not have one. The script then reports those checks as skipped rather than passing
them silently, which keeps a missing manifest from reading as agreement.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
DEFAULT_PORT = REPOSITORY / "scripts/vcpkg-ports/wolf-lang-packages"
DEFAULT_MANIFEST = REPOSITORY / "build/lang-packages/dist/manifest.json"

BUNDLE_VERSION = re.compile(r'set\(WOLF_LANG_PACKAGES_BUNDLE_VERSION "([^"]+)"\)')
SUITES = re.compile(r'set\(WOLF_LANG_PACKAGES_SUITES "([^"]+)"\)')
ASSET = re.compile(r'set\(WOLF_LANG_([A-Z0-9]+)_(FILE|SHA512|DIR) "([^"]+)"\)')


class Facts:
    """The release facts read from one file, with the origin kept for messages."""

    def __init__(self, origin):
        self.origin = origin
        self.bundle_version = None
        self.suites = []
        self.files = {}
        self.sha512 = {}
        self.features = []
        self.default_features = []
        self.manifest_files = {}
        self.manifest_sha512 = {}
        self.manifest_version = None


def read_assets(path):
    facts = Facts(path)
    text = path.read_text(encoding="utf-8")
    match = BUNDLE_VERSION.search(text)
    if not match:
        raise ValueError(f"{path}: no WOLF_LANG_PACKAGES_BUNDLE_VERSION")
    facts.bundle_version = match.group(1)
    match = SUITES.search(text)
    if not match:
        raise ValueError(f"{path}: no WOLF_LANG_PACKAGES_SUITES")
    facts.suites = match.group(1).split(";")
    for key, kind, value in ASSET.findall(text):
        suite = key.lower()
        if kind == "FILE":
            facts.files[suite] = value
        elif kind == "SHA512":
            facts.sha512[suite] = value
    return facts


def read_port(path):
    facts = Facts(path)
    document = json.loads(path.read_text(encoding="utf-8"))
    facts.bundle_version = document["version-string"]
    facts.features = sorted(document.get("features", {}))
    facts.default_features = sorted(document.get("default-features", []))
    return facts


def read_manifest(path):
    facts = Facts(path)
    document = json.loads(path.read_text(encoding="utf-8"))
    facts.manifest_version = document["bundleVersion"]
    for package in document["packages"]:
        name = package["file"]
        facts.manifest_files[name] = package["sha512"]
        facts.manifest_sha512[name] = package["sha512"]
    return facts


def check(report, condition, message):
    report.append(("ok" if condition else "fail", message))
    return condition


def compare(port, manifest):
    """Returns the report of every comparison as a list of (status, message)."""
    report = []
    check(report, port.bundle_version == manifest.bundle_version,
          f"bundle version: the port says {port.bundle_version}, "
          f"assets.cmake says {manifest.bundle_version}")
    missing = [name for name in port.features if name not in manifest.suites]
    extra = [name for name in manifest.suites if name not in port.features]
    check(report, not missing and not extra,
          f"features: the port and assets.cmake name the same suites "
          f"(port only: {missing or 'none'}, assets only: {extra or 'none'})")
    unsupported = [name for name in port.default_features if name not in port.features]
    check(report, not unsupported,
          f"default-features: every default is a feature "
          f"(unknown: {unsupported or 'none'})")
    unknown = [name for name in manifest.suites if name not in manifest.files]
    check(report, not unknown,
          f"assets.cmake: every suite has an archive (without one: {unknown or 'none'})")
    return report


def compare_manifest(report, manifest, release):
    check(report, manifest.bundle_version == release.manifest_version,
          f"release manifest: the bundle is {release.manifest_version}, "
          f"assets.cmake says {manifest.bundle_version}")
    for suite in manifest.suites:
        name = manifest.files[suite]
        if name not in release.manifest_files:
            check(report, False, f"release manifest: it does not list {name}")
        else:
            check(report, release.manifest_files[name] == manifest.sha512[suite],
                  f"release manifest: {name} carries the pinned SHA512")
    return report


def compare_dist(report, manifest, dist):
    for suite in manifest.suites:
        path = dist / manifest.files[suite]
        if not path.is_file():
            report.append(("skip", f"archives: {path.name} is absent from {dist}"))
            continue
        digest = hashlib.sha512(path.read_bytes()).hexdigest()
        check(report, digest == manifest.sha512[suite],
              f"archives: {path.name} matches the pinned SHA512")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=Path, default=DEFAULT_PORT,
                        help="directory holding assets.cmake and vcpkg.json")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
                        help="release manifest written by make-lang-release.py")
    parser.add_argument("--dist", type=Path, default=None,
                        help="directory holding the release archives "
                             "(defaults to the manifest directory)")
    arguments = parser.parse_args(argv)

    try:
        port = read_port(arguments.port / "vcpkg.json")
        manifest = read_assets(arguments.port / "assets.cmake")
    except (OSError, ValueError, KeyError) as error:
        print(f"error: cannot read the port: {error}")
        return 1

    report = compare(port, manifest)
    if arguments.manifest.is_file():
        try:
            release = read_manifest(arguments.manifest)
        except (OSError, ValueError, KeyError) as error:
            print(f"error: cannot read the release manifest: {error}")
            return 1
        compare_manifest(report, manifest, release)
        dist = arguments.dist or arguments.manifest.parent
        if dist.is_dir():
            compare_dist(report, manifest, dist)
        else:
            report.append(("skip", f"archives: {dist} does not exist"))
    else:
        report.append(("skip", f"release manifest: {arguments.manifest} does not exist, "
                               "so the archive names and SHA512 pins are unverified"))

    for status, message in report:
        print(f"{status}: {message}")
    failed = [message for status, message in report if status == "fail"]
    skipped = [message for status, message in report if status == "skip"]
    print(f"{len(report) - len(failed) - len(skipped)} check(s) passed, "
          f"{len(failed)} failed, {len(skipped)} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
