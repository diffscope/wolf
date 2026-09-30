#!/usr/bin/env python3
"""Packages the converted language resources for a release.

Each archive unpacks to a package root directory rather than to a single file, because the loader
on the synthrt main branch accepts directories only: it rejects a path that is not a directory, and
it scans search paths for subdirectories. The loader cannot load a dspk file at present.

The script writes the archives and the manifest to the directory given by --out, which is
untracked, and updates the asset list of the vcpkg port in place. That list is generated but small;
keeping it in this repository allows a single commit to tie the port to the release it describes.
"""

import argparse
import filecmp
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from declarations import load_json


def sha512(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def archive(source: Path, destination: Path) -> None:
    # Zip members carry a timestamp, and zipfile derives it from the mtime of the source file
    # through time.localtime(), so the same tree packed on a UTC host and on a UTC+08 host produced
    # different bytes and therefore a different SHA512 for the vcpkg port to pin. The members are
    # therefore stamped with a fixed time. No consumer reads that field (--verify compares
    # contents, not stat), and the same tree now produces the same hash on every host.
    stamp = (1980, 1, 1, 0, 0, 0)
    # The two other fields that from_file() derives from the stat of the source file also differ
    # between hosts: create_system is 0 on Windows and 3 elsewhere, and external_attr carries the
    # mode, which is 0o100666 on Windows and 0o100644 or 0o100664 on a POSIX host depending on its
    # umask. Both are stored in the central directory, so both are fixed as well. The values are
    # those that a POSIX host computes for a regular 0644 file, (st_mode & 0xFFFF) << 16, so that
    # Windows produces the POSIX values instead of a third variant. No consumer reads these fields:
    # zipfile ignores the mode on extraction, --verify compares the unpacked contents, and nearly
    # every archive in common use carries 3.
    create_system = 3
    external_attr = (0o100644 & 0xFFFF) << 16
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo.from_file(path, Path(source.name) / path.relative_to(source))
                info.date_time = stamp
                info.create_system = create_system
                info.external_attr = external_attr
                # from_file() returns a stored member without a compression level. writestr() is
                # the public call that accepts the codec and the level for a prepared ZipInfo, and
                # it writes through the same member stream as open(), so the archive bytes equal
                # those produced by setting the level on the ZipInfo directly. The attribute that
                # holds the level is private and was renamed in Python 3.13.
                bundle.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED,
                                compresslevel=9)


def unpack_and_compare(archives: list, sources: dict, destination: Path) -> list:
    """Unpacks every archive, compares the result against its source, and returns the problems
    found.

    A consumer receives the archive, so a correct conversion does not imply a correct release: a
    path rewritten during packing, a file missed by the glob, or a name that does not survive
    zipping would pass every earlier check. This step makes the verification part of the script
    instead of a manual step, and it keeps the unpacked tree so that the tests can run against the
    archives rather than against the conversion output.
    """
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    problems = []
    for entry in archives:
        with zipfile.ZipFile(entry["path"]) as bundle:
            bundle.extractall(destination)
        unpacked = destination / entry["directory"]
        if not unpacked.is_dir():
            problems.append(f"{entry['file']}: does not unpack to {entry['directory']}/")
            continue

        source = sources[entry["id"]]
        comparison = filecmp.dircmp(str(source), str(unpacked))
        stack = [("", comparison)]
        while stack:
            prefix, node = stack.pop()
            for name in node.left_only:
                problems.append(f"{entry['file']}: {prefix}{name} is missing from the archive")
            for name in node.right_only:
                problems.append(f"{entry['file']}: {prefix}{name} is in the archive but not in "
                                f"the source")
            for name in node.funny_files:
                problems.append(f"{entry['file']}: {prefix}{name} could not be compared")
            # A shallow comparison checks stat only, and a rewrite can preserve both size and
            # mtime. The contents are therefore compared.
            _, mismatch, errors = filecmp.cmpfiles(node.left, node.right, node.common_files,
                                                   shallow=False)
            for name in mismatch + errors:
                problems.append(f"{entry['file']}: {prefix}{name} differs from the source")
            for name, child in node.subdirs.items():
                stack.append((f"{prefix}{name}/", child))
    return problems


def read_identity(directory: Path) -> tuple[str, str]:
    desc = load_json(directory / "desc.json")
    return desc["id"], desc["version"]


def suite_of(package_id: str) -> str:
    """Returns the feature name that selects a package: cmn for wolf/lang-cmn and multi for
    wolf/g2p-multi. The port matches these names against FEATURES, so they must be the bare
    names."""
    tail = package_id.split("/")[-1]
    for prefix in ("lang-", "g2p-"):
        if tail.startswith(prefix):
            return tail[len(prefix):]
    return tail


def port_features(packages: list, sources: dict, previous: dict) -> dict:
    """Returns the features block of the port manifest, with one feature per packaged suite.

    The portfile installs a suite only if a feature of that name is selected, and vcpkg rejects a
    feature that the manifest does not declare, so the block must list exactly the suites that
    assets.cmake lists. The block is therefore derived from the same packages: a suite added to the
    release but not to a manually maintained block could not be installed.

    The feature of a suite depends on the features of the packages that its desc.json depends on,
    which is how a language selects its shared backend. An existing feature description is kept,
    because the backends carry manually written descriptions that state their size; a new feature
    receives a generated description.
    """
    packaged = {package["id"]: suite_of(package["id"]) for package in packages}
    features = {}
    for package in packages:
        suite = packaged[package["id"]]
        desc = load_json(sources[package["id"]] / "desc.json")
        needs = sorted({packaged[dependency["id"]] for dependency in desc.get("dependencies", [])
                        if dependency["id"] in packaged})
        kept = previous.get(suite, {}).get("description")
        if kept is None:
            kept = (f"Shared G2P backend {suite}" if package["id"].split("/")[-1].startswith("g2p-")
                    else f"Language resources for {suite}")
        feature = {"description": kept}
        if needs:
            feature["dependencies"] = [{
                "name": "wolf-lang-packages",
                "default-features": False,
                "features": needs,
            }]
        features[suite] = feature
    return dict(sorted(features.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converted", required=True, type=Path,
                        help="directory of the packages written by convert-g2p-packages.py")
    parser.add_argument("--extra", type=Path, nargs="*", default=[],
                        help="authored packages to include, such as wolf/lang-zxx")
    parser.add_argument("--out", required=True, type=Path,
                        help="output directory for the archives and manifest.json; emptied first")
    parser.add_argument("--bundle-version", required=True,
                        help="release version; the release tag is lang-v<bundle version>")
    parser.add_argument("--exclude", nargs="*", default=[], help="package ids to exclude")
    parser.add_argument("--port", type=Path, help="port directory whose assets.cmake to rewrite")
    parser.add_argument("--verify", type=Path,
                        help="unpack every archive into this directory and compare it against its "
                             "source; packaging fails if any archive does not round-trip")
    parser.add_argument("--skip-checks", action="store_true",
                        help="package without validating the declarations first")
    args = parser.parse_args()

    sources = [p for p in sorted(args.converted.iterdir())
               if p.is_dir() and (p / "desc.json").is_file()]
    sources += [p for p in args.extra if (p / "desc.json").is_file()]

    # The declarations are checked here instead of relying on the operator: an archive is the last
    # point at which a malformed declaration is cheap to fix, and the first point at which it
    # reaches users.
    if not args.skip_checks:
        checker = Path(__file__).resolve().parent / "check-declarations.py"
        result = subprocess.run([sys.executable, str(checker), *map(str, sources)])
        if result.returncode != 0:
            print("packaging aborted: check-declarations.py reported errors in the declarations "
                  "above", file=sys.stderr)
            return 1

    # Reject the arguments before modifying anything. The line below empties --out, and an --out
    # that contains the sources would empty them as well: this script once deleted the whole
    # conversion output because --out and --converted named the same directory. The conversion
    # output can be regenerated, but this check prevents the loss.
    for source in sources:
        try:
            source.resolve().relative_to(args.out.resolve())
        except ValueError:
            continue
        print(f"packaging aborted: --out {args.out} contains the source {source}", file=sys.stderr)
        return 1

    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)

    packages, excluded, by_id = [], [], {}
    for source in sources:
        package_id, version = read_identity(source)
        if package_id in args.exclude:
            excluded.append(package_id)
            continue
        name = f"{package_id.replace('/', '-')}-{version}.zip"
        target = args.out / name
        archive(source, target)
        by_id[package_id] = source
        packages.append({
            "id": package_id,
            "file": name,
            "sha512": sha512(target),
            "version": version,
            "compatVersion": load_json(source / "desc.json").get("compatVersion", version),
            "directory": source.name,
            "size": target.stat().st_size,
            "breaking": False,
        })

    if args.verify:
        entries = [{**p, "path": args.out / p["file"]} for p in packages]
        problems = unpack_and_compare(entries, by_id, args.verify)
        if problems:
            print("packaging aborted: the archives do not match their sources", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print(f"verified {len(entries)} archives against their sources in {args.verify}")

    manifest = {"bundleVersion": args.bundle_version, "packages": packages}
    if excluded:
        manifest["excluded"] = excluded
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=4) + "\n", encoding="utf-8")

    if args.port:
        # The port derives its download URLs from the bundle version. The release tag is
        # lang-v<bundle version>, and every archive is downloaded from that tag. If the archives
        # change and the bundle version does not, the port pins a release that does not exist, and
        # every consumer receives an HTTP 404 error or a cached file of the same name from the
        # previous release. The script therefore rejects the input at this point, while the
        # operator can still pass the correct version, instead of writing pins that no release
        # serves.
        previous = args.port / "assets.cmake"
        if previous.is_file():
            text = previous.read_text(encoding="utf-8")
            was = re.search(r'set\(WOLF_LANG_PACKAGES_BUNDLE_VERSION "([^"]*)"\)', text)
            known = dict(re.findall(r'set\(WOLF_LANG_\w+_FILE "([^"]*)"\)\s*\n'
                                    r'set\(WOLF_LANG_\w+_SHA512 "([^"]*)"\)', text))
            # The archive file name contains the package version, and vcpkg caches downloads by
            # file name. An archive whose content changes under a file name that a previous release
            # already published is therefore rejected even under a new bundle version. A consumer
            # that downloaded the earlier archive keeps a cached file of that name, and vcpkg
            # reports a hash mismatch instead of downloading the new archive. The package version
            # must be raised instead.
            reused = [p["file"] for p in packages
                      if p["file"] in known and known[p["file"]] != p["sha512"]]
            if was and was.group(1) != args.bundle_version and reused:
                print("packaging aborted: the following archives changed under a file name that "
                      "the previous release already published. Raise the version of each listed "
                      "package.", file=sys.stderr)
                for name in reused:
                    print(f"  changed under a published file name: {name}", file=sys.stderr)
                return 1
            if was and was.group(1) == args.bundle_version:
                changed = [p["file"] for p in packages
                           if known.get(p["file"]) != p["sha512"]]
                gone = [name for name in known if name not in {p["file"] for p in packages}]
                if changed or gone:
                    print(f"packaging aborted: the archives changed, but the bundle version "
                          f"remains {args.bundle_version}, and the release tag is derived from the "
                          f"bundle version. Pass a higher --bundle-version.", file=sys.stderr)
                    for name in changed:
                        print(f"  changed or new: {name}", file=sys.stderr)
                    for name in gone:
                        print(f"  no longer packaged: {name}", file=sys.stderr)
                    return 1

        lines = [
            "# Generated by scripts/make-lang-release.py. Do not edit.",
            "#",
            "# One entry per language package: the archive name, its SHA512, and the directory it",
            "# unpacks to. The portfile downloads only the archives that the selected features "
            "require.",
            "",
            f'set(WOLF_LANG_PACKAGES_BUNDLE_VERSION "{args.bundle_version}")',
            "set(WOLF_LANG_PACKAGES_SUITES \"" + ";".join(
                suite_of(p["id"]) for p in packages) + "\")",
            "",
        ]
        for package in packages:
            suite = suite_of(package["id"])
            key = suite.upper().replace("-", "_")
            lines += [
                f'set(WOLF_LANG_{key}_FILE "{package["file"]}")',
                f'set(WOLF_LANG_{key}_SHA512 "{package["sha512"]}")',
                f'set(WOLF_LANG_{key}_DIR "{package["directory"]}")',
            ]
        args.port.mkdir(parents=True, exist_ok=True)
        (args.port / "assets.cmake").write_text("\n".join(lines) + "\n", encoding="utf-8")

        # The version of the port must follow the bundle version, and its features must follow the
        # suites. assets.cmake was regenerated by this script while vcpkg.json was maintained
        # manually, so the two diverged: the manifest still specified 0.1.0.0 while the assets
        # described 0.1.1.0, and vcpkg could serve a cached tree built from the older assets under
        # the version it had already recorded. A suite added to the release without a feature of
        # the same name was the same divergence, and the portfile could not install that suite.
        manifest_path = args.port / "vcpkg.json"
        if manifest_path.is_file():
            port_manifest = load_json(manifest_path)
            before = json.dumps(port_manifest, indent=2, ensure_ascii=False) + "\n"
            if port_manifest.get("version-string") != args.bundle_version:
                print(f"port version-string -> {args.bundle_version}")
            port_manifest["version-string"] = args.bundle_version
            features = port_features(packages, by_id, port_manifest.get("features", {}))
            port_manifest["features"] = features
            if "default-features" in port_manifest:
                port_manifest["default-features"] = [
                    name for name in port_manifest["default-features"] if name in features]
            after = json.dumps(port_manifest, indent=2, ensure_ascii=False) + "\n"
            if after != before:
                manifest_path.write_text(after, encoding="utf-8")

    total = sum(p["size"] for p in packages)
    print(f"{len(packages)} archives, {total / 1024 / 1024:.1f} MiB total")
    for package in packages:
        print(f"  {package['file']:<34} {package['size'] / 1024:8.0f} KiB")
    if excluded:
        print(f"excluded: {', '.join(excluded)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
