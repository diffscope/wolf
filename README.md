# WOLF: Orchestratable Linguistic Framework

A linguistic layer for [synthrt](https://github.com/diffscope/synthrt), which adds a `language` contribute to the packages synthrt loads.

synthrt knows about singers and inferences. It does not know about languages, and it does not need to: a contribute category is registered rather than built in, so a library linked into the program can add a kind of its own. wolf is that library. A package declares a `language` the same way it declares anything else, and refers to one through the same syntax.

Nothing here is a program. It is a library for an editor or a tool to embed, alongside synthrt.

## The language contribute

A package listing a language in its `desc.json`:

```json
{
  "contributes": {
    "language": [ "./languages/mandarin.json" ]
  }
}
```

Each entry is a path to a language manifest:

```json
{
  "$version": "1.0",
  "id": "cmn",
  "name": "普通话",
  "class": "ai.svs.MandarinLanguage",
  "level": 1,
  "schema": {
    "phonemes": [ "a", "o", "e", "i", "u", "v" ]
  },
  "configuration": {
    "dict": "./dict.txt",
    "useTone": true
  }
}
```

`class` names the provider that implements the language and `level` the API version the manifest was written against. wolf resolves the provider through synthrt's plugin factory, checks it is new enough, and hands it `schema` and `configuration` to interpret. wolf itself never reads inside those two objects, which is what lets one provider describe a dictionary-driven language and another something else entirely.

The split follows the one synthrt already uses. `schema` is what the language declares about itself, which a consumer reads to decide whether it can work with it at all. `configuration` is the resources behind it, and relative paths in it resolve against the manifest's own directory.

A language is referred to like any other contribute:

```
vendor/sample=1.0:language/cmn    # fully qualified
vendor/sample:language/cmn        # version resolved from the dependencies
:language/cmn                     # within the package doing the referring
```

## Providing a language

Implement `wolf::LanguageProvider` and expose it through a `wolf::LanguageProviderPlugin`:

```cpp
class MandarinProviderPlugin : public wolf::LanguageProviderPlugin {
public:
    const char *key() const override { return "ai.svs.MandarinLanguage"; }
    srt::NO<wolf::LanguageProvider> create() override {
        return srt::NO<MandarinProvider>::create();
    }
};

SYNTHRT_EXPORT_PLUGIN(MandarinProviderPlugin)
```

The plugin is a shared library exporting `synthrt_plugin_instance`, found by its interface id and the key a manifest asks for. `cmn`, in `src/plugins/languageproviders`, is that plugin for Mandarin, and its types are declared in `wolf/Api/Languages/Mandarin/1/MandarinApiL1.h` so a consumer can cast what it produced back to something it can read:

```cpp
auto spec = pkg.contribute("language", "cmn")->as<wolf::LanguageSpec>();
auto config = spec->configuration().as<wolf::Api::Mandarin::L1::MandarinConfiguration>();
```

## Why wolf must be linked, not loaded

A `SynthUnit` reads the list of registered categories once, when it is constructed. wolf registers `language` before `main`, so any unit built afterwards has it. A plugin could not do the same: plugins load lazily *through* a unit, so by the time one runs its static initializers, that unit has already built its categories. Contributing a category is something a linked library does.

## Requirements

CMake 3.19 or later.

+ [synthrt](https://github.com/diffscope/synthrt)
+ [stdcorelib](https://github.com/SineStriker/stdcorelib)
+ [qmsetup](https://github.com/stdware/qmsetup)

Boost.Test is needed only to build the tests.

## Setup Environment

### VCPKG Packages

```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat          # or ./bootstrap-vcpkg.sh

vcpkg install --x-manifest-root=../scripts/vcpkg-manifest --x-install-root=./installed
```

Add `--x-feature=tests` to bring in Boost.Test.

### Build

synthrt is not a vcpkg dependency here. Build and install it separately, then point at that install:

```sh
cmake -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=<synthrt install dir> \
    -DCMAKE_INSTALL_PREFIX=<dir> \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all
cmake --build build --target install
```

## How to Use

```cmake
cmake_minimum_required(VERSION 3.16)

project(example)

find_package(wolf CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example wolf::wolf)
```

Linking wolf is what registers the category, so an executable that only wants languages available still links it directly rather than relying on a transitive dependency being pulled in.
