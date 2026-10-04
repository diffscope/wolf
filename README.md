# WOLF: Orchestratable Linguistic Framework

A linguistic layer for [synthrt](https://github.com/diffscope/synthrt), which adds the `linguist` contribution category to the packages synthrt loads.

synthrt knows about singers and inferences. It does not know about linguist contributions, and it does not need to: a contribution category is registered rather than built in, so a library linked into the program can add a kind of its own. wolf is that library. A package declares `linguist` contributions the same way it declares anything else and refers to one through the same syntax.

Nothing here is a program. It is a library for an editor or a tool to embed, alongside synthrt.

## The Linguist Contribution

A package lists a linguist in the `contributions.linguist` array of its `desc.json`. Each entry is
`{ "id": ..., "path": ... }` and points to a linguist manifest; the entry form is common to all
categories (spec 2.4:219) and is specified for this category in
[docs/linguist-domain-contract.md](docs/linguist-domain-contract.md) §1.1, so it is not repeated here.
A linguist manifest looks like this:

```json
{
  "name": "普通话",
  "interface": "org.openvpi.wolf.linguist.WolfLinguist",
  "level": 1,
  "variant": "wolf",
  "language": "cmn",
  "scheme": "pinyin",
  "exports": {
    "phonemes": [ "a", "o", "e", "i", "u", "v" ]
  },
  "configuration": {},
  "imports": [
    { "role": "linguist/g2p", "ref": ":inference/g2p" },
    { "role": "linguist/s2p", "ref": ":inference/s2p" }
  ]
}
```

The `(interface, level, variant)` triple selects a provider through synthrt's interpreter discovery. The provider interprets `exports`, `configuration`, import options, execution factories, validators, and extensions for that contract.

`language` and `scheme` are fields that the `linguist` category adds to every declaration and reads before a provider is selected; `exports` contains the declaration of the linguist about itself, and `configuration` the provider-specific resources. Relative paths resolve against the declaration file's directory. The category, its identity fields and the singer-side mapping are specified in [docs/linguist-domain-contract.md](docs/linguist-domain-contract.md).

A linguist is referred to like any other contribute:

```
vendor/sample:linguist/mandarin        # a module in a resolved direct dependency
:linguist/mandarin                     # a module in the referring package
```

A reference carries no version: the version of `vendor/sample` to which it binds comes from the entry
for that package in the `dependencies` of the referring package (spec 2.4:332), and a reference to a
module of the referring package itself uses the leading `:` form (spec 2.4:333).

## Providing a linguist

Implement `wolf::LinguistProvider` and expose it through a `wolf::LinguistProviderPlugin`. The provider owns all contract-specific behavior, including import options, execution factories, validators, and spec extensions:

```cpp
class MyLinguistProviderPlugin : public wolf::LinguistProviderPlugin {
public:
    srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
        create(std::string_view interfaceName, int level, std::string_view variant) override {
        // Validate the requested contract and return its provider.
    }
};

STDC_EXPORT_PLUGIN(MyLinguistProviderPlugin)
```

The bundled implementation lives in `src/plugins/linguistproviders/wolf`. Its `WolfLinguistProvider` supports `org.openvpi.wolf.linguist.WolfLinguist`, Level 1, variant `wolf`. The wolf library registers the category and defines the provider abstraction, but it does not create Wolf Level 1 runtime objects itself.

```cpp
auto spec = package->contribution("linguist", "mandarin")
                 ->as<wolf::LinguistSpec>();
```

## Why wolf must be linked, not loaded

A `SynthUnit` reads the list of registered categories once, when it is constructed. wolf registers `linguist` before `main`, so any unit built afterwards has it. A plugin could not do the same: plugins load lazily *through* a unit, so by the time one runs its static initializers, that unit has already built its categories. Contributing a category is something a linked library does.

Linking alone is not sufficient. A linker that drops unreferenced libraries, as ELF linkers do under `--as-needed` and the MSVC linker does for every import library, drops wolf from a host that uses only the category and references no wolf symbol. Such a host calls `wolf::linkLinguistCategory()` once before it constructs a unit. The function has an empty body; the reference to it keeps the library on the link line.

## Versioning and ABI

The current version is 0.1.0.0. **No ABI guarantee applies before version 1.0.** Most types that a
host uses are value types in public headers, namely `LanguageStatus`, `LanguageEntry`,
`SingerEntry`, `SingerRef` and every payload under `Api/`. A new field changes the size of such a
type, and a new virtual function changes the vtable of a contract interface such as
`WolfPipelineExtension`. Mixing objects compiled against two versions of these headers is undefined
behavior rather than a link error, and therefore produces no diagnostic.

The public headers are those under `Api/`, `Linguist/` and `Session/`, together with the root header
`wolf/wolf_global.h`; the package installs no other header. `wolf/Support` contains the internal
helpers of the plugins in this repository and is not installed. Its stateless helpers are compiled
into the static library `wolfsupport` with hidden visibility; libwolf and each plugin link that
library privately, and no binary exports its
symbols. The two components that must exist once per process, the resource cache and the log
category, remain in libwolf and are exported under `WOLF_INTERNAL_EXPORT` for use by the plugins
of this repository. These symbols are not public API, carry no ABI guarantee at any version, and
must not be linked by a third-party provider. A provider built outside this repository depends
only on the installed headers and on synthrt.

**A host must be rebuilt whenever the version of wolf changes.** The version is raised in the same
commit as every ABI-breaking change, so a changed version number indicates that a rebuild is
required.
The installed CMake package is written with `ExactVersion` compatibility, so `find_package(wolf)`
with a version accepts only that exact version. `wolf::LinguistSession` and `wolf::CancelToken` are
the two public types held behind a pimpl, and their layouts are therefore stable across versions.

## Requirements

CMake 3.19 or later, and a checkout of the vcpkg overlay submodule:

```sh
git submodule update --init
```

Every dependency, including synthrt, is installed through vcpkg from the manifest in
`scripts/vcpkg-manifest`. Ports resolve through two overlays in order: `scripts/vcpkg-ports` in
this repository, which holds the `synthrt-main` and `wolf-lang-packages` ports, and then the shared
`scripts/vcpkg` submodule.

+ [synthrt](https://github.com/diffscope/synthrt), through the in-repo `synthrt-main` port. The
  port pins a commit on the synthrt branch `onnxruntime-builds-uptake`, which is based on the
  synthrt main line. The `synthrt` port of the shared overlay pins the refactor line and is not
  used here.
+ [stdcorelib](https://github.com/SineStriker/stdcorelib)
+ [qmsetup](https://github.com/stdware/qmsetup)
+ [BLAKE3](https://github.com/BLAKE3-team/BLAKE3), for content hashing in the resource cache.
+ [RE2](https://github.com/google/re2), for word classification patterns. The shipped language
  packages use Unicode property classes such as `\p{Han}`, which `std::regex` does not support.
+ [cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin), the G2P engine for Mandarin and Cantonese.

Boost.Test is required only to build the tests.

Two variants depend on an external backend and are built only if that backend is present, in the
same way as synthrt omits its ONNX driver if ONNX Runtime is absent:

+ `multig2p-onnx`, the shared model backend, requires dsinfer and its ONNX Runtime driver. The
  `onnx` feature (`--x-feature=onnx`) adds `synthrt-main[onnx]`; ONNX Runtime itself comes from
  the `onnxruntime-builds` port of the shared overlay (DirectML on Windows, CPU on other platforms,
  no CUDA).
+ `lua`, the scripted S2P and Onset variants, requires LuaJIT, which the manifest lists as a plain
  dependency.

`-DWOLF_DISABLE_DSINFER=ON` and `-DWOLF_DISABLE_LUAJIT=ON` exclude either backend even if it is
installed. A build without either backend still loads every package that the shipped plugins
support. The test stub always declares a `stub-miscount` variant for the fixtures that violate the
provider contract deliberately, and declares `multig2p-onnx` only in a build without dsinfer, so
that the loading tests also run in that build. The shipped minimal build contains no stub, and a
package that requires the model backend fails to load in it.

## Setup Environment

### VCPKG Packages

```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat          # or ./bootstrap-vcpkg.sh

vcpkg install --x-manifest-root=../scripts/vcpkg-manifest --x-install-root=./installed
```

The manifest defines three optional features:

| Feature | Effect |
| :-- | :-- |
| `onnx` | Builds dsinfer and its ONNX Runtime driver through `synthrt-main[onnx]`, which enables the `multig2p-onnx` variant. |
| `lang-packages` | Installs the default suites of the published language packages (`cmn`, `jpn`, `yue`, `zxx` and the backends they require) through the `wolf-lang-packages` port, for use by the tests. |
| `tests` | Adds Boost.Test. |

> The pin of the overlay submodule must match the pin that synthrt uses. Two revisions of
> `stdcorelib-plugin` share the version string `0.1.0.0#1` but fetch different upstream refs, and
> they name the CMake helper differently (`stdc_add_plugin_metadata` and
> `stdc_add_plugin_manifest`). A mismatched pin therefore fails at configure time.

### Build

```sh
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_INSTALLED_DIR=<install root passed above> \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DCMAKE_INSTALL_PREFIX=<dir> \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all
cmake --build build --target install
```

### Tests

`-DWOLF_BUILD_TESTS=ON` builds the tests and registers them with CTest; `ctest --test-dir build`
runs them. A full build registers 20 test binaries. A build without LuaJIT omits
`test_LuaVariants`, and a build without dsinfer omits `test_MultiG2P` and `test_MultiG2PMaxLen`
(the registration conditions are the single source of truth for the list:
`src/tests/auto/CMakeLists.txt`).

The tests that load real language packages read them from the location given by the cache
variable `WOLF_LANG_PACKAGES_SOURCE`, which names a directory of unpacked packages. If the variable
is empty, the build uses the tree installed by the `wolf-lang-packages` port (the `lang-packages`
feature). The current release, with its bundle version and the version and file name of each
archive, is listed in [docs/linguist-distribution.md](docs/linguist-distribution.md) §5.1 (the
authoritative table; not copied here to avoid drift).

The fixture packages that seven of the automated tests read are generated by
`scripts/make-test-fixtures.py`, and the repository carries the generator rather than the packages.
Point the cache variable `WOLF_TEST_FIXTURES_SOURCE` at a directory that the script produced:

```sh
python3 scripts/make-test-fixtures.py --output build/test-fixtures
cmake -B build -DWOLF_TEST_FIXTURES_SOURCE=build/test-fixtures ...
```

`--check` compares an existing directory against the script and reports every difference, which is
how the packages were verified byte for byte before they stopped being committed.

The host flow test additionally reads the voicebank fixture that
`scripts/make-voicebank-fixture.py` generates, from the cache variable
`WOLF_VOICEBANK_FIXTURE_SOURCE`. A test whose data is absent exits with status 77, which CTest
reports as skipped.

CI (`.github/workflows/ci.yml`) builds with the `onnx` and `tests` features on Linux and Windows,
and adds `lang-packages` on Linux so that the release the port pins is exercised. It runs the self
tests of the declaration lint and the lint on `packages/wolf-lang-zxx`, runs CTest,
and then installs wolf and builds `.github/consumer` against the installed package. The consumer
check verifies the `wolf::wolf` target, `WOLF_PLUGINS_DIR` and the absence of Support headers.

### Installed Layout

The package installs the headers under `include/`, the library, and the CMake package config. The
plugins are installed under `lib/plugins/wolf/<category>/<name>/`, where `<category>` is
`inferenceinterpreters` or `linguistproviders`; the build tree uses the same layout. The config
defines `WOLF_PLUGINS_DIR` as the plugin directory of the release tree. If a `debug/` prefix tree
exists, as in a vcpkg installation, the config also defines `WOLF_PLUGINS_DIR_DEBUG`. A host passes
each category directory to `SynthUnit::setPluginPaths`.

## How to Use

```cmake
cmake_minimum_required(VERSION 3.19)

project(example)

find_package(wolf CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example wolf::wolf)
```

Linking wolf is what registers the category, so an executable that only wants linguist contributions available still links it directly rather than relying on a transitive dependency being pulled in.
