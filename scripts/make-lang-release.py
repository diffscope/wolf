#!/usr/bin/env python3
"""Package the converted language resources for a release.

Each archive unpacks to a Package root directory rather than to a single file, because the loader
in synthrt main accepts directories only: it rejects a path that is not one, and it scans search
paths for subdirectories. A dspk file could not be loaded today.

Writes the archives and manifest to the conversion output directory, which is not tracked, and
updates the vcpkg port's asset list in place. That list is generated but small, and keeping it in
this repository is what lets one commit tie the port to the release it describes.
"""

import argparse
import filecmp
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile
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


def sha512(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def archive(source: Path, destination: Path) -> None:
    # Zip members carry a timestamp, and zipfile takes it from the source file's mtime through
    # time.localtime(), so one tree packed on a UTC host and on a +08 host produced different
    # bytes -- and therefore a different SHA512 for the vcpkg port to pin. Stamp the members
    # instead: nothing reads that field (--verify compares contents, not stat), so one tree now
    # hashes the same everywhere.
    stamp = (1980, 1, 1, 0, 0, 0)
    # The remaining two fields from_file() reads out of the source file's stat do not describe the
    # same file on two hosts either: create_system is 0 on Windows and 3 elsewhere, and
    # external_attr carries the mode, which is 0o100666 on Windows and 0o100644 or 0o100664 on a
    # POSIX host depending on its umask. Both land in the central directory, so pin them as well.
    # The values are the ones a POSIX host computes for a regular 0644 file, (st_mode & 0xFFFF)
    # << 16, so the Windows side agrees with the POSIX one rather than adding a third spelling.
    # Nothing reads them: zipfile ignores the mode on extraction, --verify compares the unpacked
    # contents, and 3 is what nearly every archive in the wild carries.
    create_system = 3
    external_attr = (0o100644 & 0xFFFF) << 16
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo.from_file(path, Path(source.name) / path.relative_to(source))
                info.date_time = stamp
                info.create_system = create_system
                info.external_attr = external_attr
                # from_file() hands back a stored, level-less member: its callers (write(),
                # writestr()) are the ones that pass it the codec and the level, and this loop
                # goes directly to open(). State both so the members stay deflated at level 9,
                # the way write() had them. 3.13 renamed _compresslevel to compress_level and
                # kept the old name as an alias.
                info.compress_type = zipfile.ZIP_DEFLATED
                info._compresslevel = 9
                with path.open("rb") as handle, bundle.open(info, "w") as member:
                    shutil.copyfileobj(handle, member, 1024 * 8)


def unpack_and_compare(archives: list, sources: dict, destination: Path) -> list:
    """Unpacks every archive and compares the result against what went in.

    An archive is what a consumer actually gets, so "the conversion is correct" is not the same
    claim as "the release is correct": a path rewritten on the way in, a file missed by the glob,
    a name that does not survive zipping would all pass every check made before this point. This
    is the step that turns "verified" from something someone remembers doing into something the
    script did, and it leaves the unpacked tree behind so the tests can run against the archives
    rather than against the conversion output.
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
                problems.append(f"{entry['file']}: {prefix}{name} did not make it into the archive")
            for name in node.right_only:
                problems.append(f"{entry['file']}: {prefix}{name} is in the archive but not the "
                                f"source")
            for name in node.funny_files:
                problems.append(f"{entry['file']}: {prefix}{name} could not be compared")
            # Shallow compares stat only, and a rewrite can keep both size and time. Read them.
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converted", required=True, type=Path)
    parser.add_argument("--extra", type=Path, nargs="*", default=[],
                        help="authored packages to include, such as wolf/lang-zxx")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--bundle-version", required=True)
    parser.add_argument("--exclude", nargs="*", default=[], help="package ids to leave out")
    parser.add_argument("--port", type=Path, help="port directory whose assets.cmake to rewrite")
    parser.add_argument("--verify", type=Path,
                        help="unpack every archive here and compare it against the source; "
                             "packaging fails if any archive does not round trip")
    parser.add_argument("--skip-checks", action="store_true",
                        help="package without validating the declarations first")
    args = parser.parse_args()

    sources = [p for p in sorted(args.converted.iterdir())
               if p.is_dir() and (p / "desc.json").is_file()]
    sources += [p for p in args.extra if (p / "desc.json").is_file()]

    # Checked here rather than left to whoever remembers: an archive is the last point at which a
    # malformed declaration is cheap to fix, and the first at which it becomes someone's download.
    if not args.skip_checks:
        checker = Path(__file__).resolve().parent / "check-declarations.py"
        result = subprocess.run([sys.executable, str(checker), *map(str, sources)])
        if result.returncode != 0:
            print("packaging refused: the declarations above do not check out", file=sys.stderr)
            return 1

    # Refuse before touching anything. The line below empties --out, and an --out that holds the
    # sources would empty those too: this script once deleted the whole conversion output because
    # --out and --converted named the same directory, and the sources it had just listed went with
    # it. Regenerable, but there is no reason to find that out by doing it.
    for source in sources:
        try:
            source.resolve().relative_to(args.out.resolve())
        except ValueError:
            continue
        print(f"refusing: --out {args.out} contains the source {source}", file=sys.stderr)
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
            print("packaging refused: the archives do not match their sources", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print(f"verified {len(entries)} archives against their sources in {args.verify}")

    manifest = {"bundleVersion": args.bundle_version, "packages": packages}
    if excluded:
        manifest["excluded"] = excluded
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=4) + "\n", encoding="utf-8")

    def suite_of(package_id: str) -> str:
        """The feature name a package is selected by: wolf/lang-cmn is cmn, wolf/g2p-multi is
        multi. The port matches these against FEATURES, so they have to be the bare names."""
        tail = package_id.split("/")[-1]
        for prefix in ("lang-", "g2p-"):
            if tail.startswith(prefix):
                return tail[len(prefix):]
        return tail

    if args.port:
        lines = [
            "# Generated by scripts/make-lang-release.py. Do not edit.",
            "#",
            "# One entry per language package: the archive name, its SHA512, and the directory it",
            "# unpacks to. The portfile downloads only the ones the selected features ask for.",
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

        # The port's own version has to move with the bundle. assets.cmake was regenerated here
        # and vcpkg.json was maintained by hand, so the two drifted: the manifest still said
        # 0.1.0.0 while the assets described 0.1.1.0, and vcpkg would happily serve a cached tree
        # built from the older assets under the version it had already seen.
        manifest_path = args.port / "vcpkg.json"
        if manifest_path.is_file():
            port_manifest = load_json(manifest_path)
            if port_manifest.get("version-string") != args.bundle_version:
                port_manifest["version-string"] = args.bundle_version
                manifest_path.write_text(
                    json.dumps(port_manifest, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")
                print(f"port version-string -> {args.bundle_version}")

    total = sum(p["size"] for p in packages)
    print(f"{len(packages)} archives, {total / 1024 / 1024:.1f} MiB total")
    for package in packages:
        print(f"  {package['file']:<34} {package['size'] / 1024:8.0f} KiB")
    if excluded:
        print(f"excluded: {', '.join(excluded)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
