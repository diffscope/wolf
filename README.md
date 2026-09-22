# WOLF: Orchestratable Linguistic Framework

A linguistic layer for [synthrt](https://github.com/diffscope/synthrt), which adds the `linguist` contribution category to the packages synthrt loads.

synthrt knows about singers and inferences. It does not know about linguist contributions, and it does not need to: a contribution category is registered rather than built in, so a library linked into the program can add a kind of its own. wolf is that library. A package declares `linguist` contributions the same way it declares anything else and refers to one through the same syntax.

Nothing here is a program. It is a library for an editor or a tool to embed, alongside synthrt.

## The Linguist Contribution

A package listing a linguist in its `desc.json`:

```json
{
  "contributions": {
    "linguist": [
      { "id": "mandarin", "path": "./linguists/mandarin.json" }
    ]
  }
}
```

Each entry is a path to a linguist manifest:

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

The split follows the one synthrt already uses. `language` and `scheme` are fields the `linguist` category adds to every declaration and reads before any provider is chosen: together they are the linguist's identity, an ISO 639-3 code and the notation its pronunciations are written in. `exports` is what the linguist declares about itself. `configuration` contains provider-specific resources. Relative paths resolve against the declaration file's directory.

A linguist is referred to like any other contribute:

```
vendor/sample:linguist/mandarin        # a module in a resolved direct dependency
:linguist/mandarin                     # a module in the referring package
```

A reference carries no version: which version of `vendor/sample` it binds to is decided by that
package's `dependencies` entry, and the reference always follows it. The package id must appear in
`dependencies`, and a module in the referring package must use the leading `:` form.

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

Linking is not quite enough on its own. A linker that drops libraries nothing references, which ELF linkers do under `--as-needed` and the MSVC linker does for every import library, would drop wolf from a host that only wants the category and names no wolf symbol. Such a host calls `wolf::linkLinguistCategory()` once before it constructs a unit. The function does nothing; being named is its whole job.

## Versioning and ABI

**wolf makes no ABI promise before 1.0.** Most of what a host touches is a value type in a public
header — `LanguageStatus`, `LanguageEntry`, `SingerEntry`, `SingerRef`, and every payload under
`Api/` — so adding a field to any of them changes its size, and the contract interfaces such as
`WolfPipelineExtension` change their vtable when a virtual is added. Mixing objects compiled
against two versions of these headers is undefined behaviour, not a link error, so it will not
announce itself.

The practical rule: **rebuild the host whenever wolf's version changes.** wolf moves its own
version in the same commit as any such change, so the number is a reliable signal that a rebuild is
needed. `wolf::LinguistSession` is the one type held behind a pimpl and therefore the one that does
not move.

## Requirements

CMake 3.19 or later, and a checkout of the vcpkg overlay submodule:

```sh
git submodule update --init
```

Every dependency, synthrt included, comes from vcpkg. Ports resolve through two overlays, searched
in order: `scripts/vcpkg-ports` in this repository, then the shared `scripts/vcpkg` submodule.

+ [synthrt](https://github.com/diffscope/synthrt) — via the in-repo `synthrt-main` port, which
  pins the main line. The shared overlay's `synthrt` port pins a different line and is not used
  here.
+ [stdcorelib](https://github.com/SineStriker/stdcorelib)
+ [qmsetup](https://github.com/stdware/qmsetup)
+ [BLAKE3](https://github.com/BLAKE3-team/BLAKE3) — content hashing for the resource cache
+ [RE2](https://github.com/google/re2) — word classification patterns. The shipped language
  packages use Unicode property classes such as `\p{Han}`, which `std::regex` cannot express
+ [cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin) — the Mandarin and Cantonese G2P engine

Boost.Test is needed only to build the tests.

Two variants carry an external backend and are built only where it is present, the same way
synthrt drops its own ONNX driver when ONNX Runtime is absent:

+ `multig2p-onnx`, the shared model backend, needs dsinfer and its ONNX Runtime driver. Add the
  `onnx` feature (`--x-feature=onnx`) to bring in `synthrt-main[onnx]`; the payload comes from the
  shared overlay's `onnxruntime-builds` port (DirectML on Windows, CPU elsewhere, no CUDA).
+ `lua`, the scripted S2P and Onset variants, needs LuaJIT, which is a plain dependency.

A build without either still loads every package the shipped plugins can serve. The test stub
always declares a `stub-miscount` variant for the fixtures that break the provider contract on
purpose, and declares `multig2p-onnx` only in a build without dsinfer, so that the loading tests
can run there too; the shipped minimal build carries no stub, and a package needing the model
backend fails to load in it.

## Setup Environment

### VCPKG Packages

```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat          # or ./bootstrap-vcpkg.sh

vcpkg install --x-manifest-root=../scripts/vcpkg-manifest --x-install-root=./installed
```

Add `--x-feature=tests` to bring in Boost.Test.

> The overlay submodule pin must match the one synthrt itself uses. Two revisions of
> `stdcorelib-plugin` share the version string `0.1.0.0#1` while fetching different upstream
> refs, and they disagree on whether the CMake helper is called `stdc_add_plugin_metadata` or
> `stdc_add_plugin_manifest`, so a mismatched pin fails at configure time.

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

Add `-DWOLF_BUILD_TESTS=ON` to build and register the tests, then run them with `ctest`.

## How to Use

```cmake
cmake_minimum_required(VERSION 3.16)

project(example)

find_package(wolf CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example wolf::wolf)
```

Linking wolf is what registers the category, so an executable that only wants linguist contributions available still links it directly rather than relying on a transitive dependency being pulled in.
