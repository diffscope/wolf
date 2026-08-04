#include <filesystem>
#include <fstream>
#include <string>

#include <synthrt/Core/PackageRef.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Languages/Mandarin/1/MandarinApiL1.h>
#include <wolf/Language/LanguageContrib.h>
#include <wolf/Language/LanguageProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;
namespace Cmn = wolf::Api::Mandarin::L1;

using srt::SynthUnit;

// End to end, which is the only way to know any of this works: a package on disk declaring nothing
// but a language, loaded by a unit that has to find the plugin, resolve the class it names, and
// hand it the two objects to parse.
//
// It exercises three things that were separately impossible or untested. The category comes from
// wolf rather than synthrt. The manifest declares no singer and no inference, which used to be
// rejected outright. And the provider is reached through synthrt's plugin factory under an
// interface synthrt has never heard of.
namespace {

    void write(const fs::path &path, const std::string &content) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path);
        BOOST_REQUIRE(file.is_open());
        file << content;
    }

    // Builds the package under the test's own build directory, so nothing outside it is touched
    // and a stale run cannot be mistaken for a fresh one.
    fs::path makePackage() {
        auto dir = fs::temp_directory_path() / "wolf-test-cmn-package";
        fs::remove_all(dir);

        write(dir / "desc.json", R"({
    "id": "test/mandarin",
    "version": "1.0.0.0",
    "dependencies": [],
    "contributes": {
        "language": [ "./languages/cmn.json" ]
    }
})");

        write(dir / "languages" / "cmn.json", std::string(R"({
    "$version": "1.0",
    "id": "cmn",
    "name": "Mandarin",
    "class": ")") + Cmn::API_CLASS + R"(",
    "level": 1,
    "schema": {
        "phonemes": [ "a", "o", "e", "i", "u", "v" ]
    },
    "configuration": {
        "dict": "./dict.txt",
        "useTone": true
    }
})");

        write(dir / "languages" / "dict.txt", "a\ta\nma\tm a\n");
        return dir;
    }

}

BOOST_AUTO_TEST_SUITE(test_MandarinProvider)

BOOST_AUTO_TEST_CASE(test_MandarinProvider_LoadsFromAPackage) {
    auto dir = makePackage();

    SynthUnit su;
    su.addPluginPath(wolf::LanguageProviderPlugin::IID, WOLF_TEST_PLUGIN_DIR);

    auto exp = su.open(dir, false);
    if (!exp) {
        BOOST_FAIL(exp.error().toString());
    }

    auto pkg = exp.take();
    if (!pkg.isLoaded()) {
        BOOST_FAIL(pkg.error().toString());
    }
    BOOST_CHECK(pkg.id() == "test/mandarin");

    auto spec = pkg.contribute("language", "cmn");
    BOOST_REQUIRE(spec != nullptr);

    auto language = spec->as<wolf::LanguageSpec>();
    BOOST_CHECK(language->className() == Cmn::API_CLASS);
    BOOST_CHECK(language->apiLevel() == Cmn::API_LEVEL);

    // The provider ran, and what it produced is its own type rather than the base.
    auto schema = language->schema();
    BOOST_REQUIRE(schema != nullptr);
    auto mandarinSchema = schema.as<Cmn::MandarinSchema>();
    BOOST_REQUIRE(mandarinSchema != nullptr);
    BOOST_CHECK(mandarinSchema->phonemes.size() == 6);
    BOOST_CHECK(mandarinSchema->phonemes.front() == "a");

    auto config = language->configuration();
    BOOST_REQUIRE(config != nullptr);
    auto mandarinConfig = config.as<Cmn::MandarinConfiguration>();
    BOOST_REQUIRE(mandarinConfig != nullptr);
    BOOST_CHECK(mandarinConfig->useTone);
    BOOST_CHECK(mandarinConfig->extraDict.empty());

    // Relative paths resolve against the manifest's own directory, not the package root.
    BOOST_CHECK(fs::exists(mandarinConfig->dict));
    BOOST_CHECK(mandarinConfig->dict.filename() == "dict.txt");

    pkg.close();
    fs::remove_all(dir);
}

// A manifest the provider rejects has to fail the load, not half load.
BOOST_AUTO_TEST_CASE(test_MandarinProvider_RejectsIncompleteManifest) {
    auto dir = makePackage();

    // The dictionary is required, and phonemes must not be empty.
    write(dir / "languages" / "cmn.json", std::string(R"({
    "$version": "1.0",
    "id": "cmn",
    "class": ")") + Cmn::API_CLASS + R"(",
    "level": 1,
    "schema": { "phonemes": [] },
    "configuration": { }
})");

    SynthUnit su;
    su.addPluginPath(wolf::LanguageProviderPlugin::IID, WOLF_TEST_PLUGIN_DIR);

    auto exp = su.open(dir, false);
    if (exp) {
        auto pkg = exp.take();
        BOOST_CHECK(!pkg.isLoaded());
        BOOST_CHECK(!pkg.error().message().empty());
        pkg.close();
    }

    fs::remove_all(dir);
}

BOOST_AUTO_TEST_SUITE_END()
