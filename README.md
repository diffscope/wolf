# WOLF: Orchestratable Linguistic Framework

A linguistic layer for [synthrt](https://github.com/diffscope/synthrt), which adds the `org.openvpi.language` contribution category to the packages synthrt loads.

synthrt knows about singers and inferences. It does not know about languages, and it does not need to: a contribution category is registered rather than built in, so a library linked into the program can add a kind of its own. wolf is that library. A package declares `org.openvpi.language` contributions the same way it declares anything else and refers to one through the same syntax.

Nothing here is a program. It is a library for an editor or a tool to embed, alongside synthrt.

## The language contribute

A package listing a language in its `desc.json`:

```json
{
  "contributions": {
    "org.openvpi.language": [
      { "id": "mandarin", "path": "./languages/mandarin.json" }
    ]
  }
}
```

Each entry is a path to a language manifest:

```json
{
  "name": "普通话",
  "interface": "org.openvpi.wolf.language.WolfLanguage",
  "level": 1,
  "variant": "wolf",
  "exports": {
    "phonemes": [ "a", "o", "e", "i", "u", "v" ]
  },
  "configuration": {},
  "imports": [
    { "role": "g2p", "ref": ":inference/g2p" },
    { "role": "s2p", "ref": ":inference/s2p" }
  ]
}
```

The `(interface, level, variant)` triple selects a provider through synthrt's interpreter discovery. The provider interprets `exports`, `configuration`, import options, execution factories, validators, and extensions for that contract.

The split follows the one synthrt already uses. `exports` is what the language declares about itself. `configuration` contains provider-specific resources. Relative paths resolve against the declaration file's directory.

A language is referred to like any other contribute:

```
vendor/sample=1.0:org.openvpi.language/mandarin    # fully qualified
vendor/sample:org.openvpi.language/mandarin        # version resolved from dependencies
:org.openvpi.language/mandarin                     # within the referring package
```

## Providing a language

Implement `wolf::LanguageProvider` and expose it through a `wolf::LanguageProviderPlugin`. The provider owns all contract-specific behavior, including import options, execution factories, validators, and spec extensions:

```cpp
class MyLanguageProviderPlugin : public wolf::LanguageProviderPlugin {
public:
    srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
        create(std::string_view interfaceName, int level, std::string_view variant) override {
        // Validate the requested contract and return its provider.
    }
};

STDC_EXPORT_PLUGIN(MyLanguageProviderPlugin)
```

The bundled implementation lives in `src/plugins/languageproviders/wolf`. Its `WolfLanguageProvider` supports `org.openvpi.wolf.language.WolfLanguage`, Level 1, variant `wolf`. The wolf library registers the category and defines the provider abstraction, but it does not create Wolf Level 1 runtime objects itself.

```cpp
auto *spec = package->contribution("org.openvpi.language", "mandarin")
                 ->as<wolf::LanguageSpec>();
```

## Why wolf must be linked, not loaded

A `SynthUnit` reads the list of registered categories once, when it is constructed. wolf registers `org.openvpi.language` before `main`, so any unit built afterwards has it. A plugin could not do the same: plugins load lazily *through* a unit, so by the time one runs its static initializers, that unit has already built its categories. Contributing a category is something a linked library does.

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
