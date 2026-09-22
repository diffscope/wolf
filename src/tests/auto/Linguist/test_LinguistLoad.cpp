#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace {

    /// Points a unit at both plugin categories.
    ///
    /// The linguist category belongs to wolf, the inference category to synthrt, so they are
    /// separate search paths even though both plugins are built here. A search path holds one
    /// subdirectory per plugin, which is why these are the parents of the plugin directories.
    void configurePlugins(srt::SynthUnit &unit) {
        std::vector<fs::path> linguist = {fs::path(WOLF_TEST_LINGUIST_PLUGIN_DIR)};
        std::vector<fs::path> inference = {fs::path(WOLF_TEST_INFERENCE_PLUGIN_DIR)};
        unit.setPluginPaths(wolf::LINGUIST_CATEGORY, linguist);
        unit.setPluginPaths("inference", inference);
    }

    fs::path packagePath(const char *name) {
        return fs::path(WOLF_TEST_PACKAGE_DIR) / name;
    }

    fs::path fixturePath(const char *name) {
        return fs::path(WOLF_TEST_FIXTURE_DIR) / name;
    }

}

BOOST_AUTO_TEST_SUITE(test_LinguistLoad)

/// Loading wolf/lang-zxx end to end is what actually proves the contract family sits in synthrt's
/// built-in inference category: the loader has to discover the stub plugin through that category's
/// own IID and search path, select all three triples, and hand the results back as typed exports.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_LoadsPassthroughPackage) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(packagePath("wolf-lang-zxx"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("wolf/lang-zxx should have loaded: " + handle.error().toString());
    }
    BOOST_CHECK(handle->isLoaded());

    auto *spec = handle->contribution(wolf::LINGUIST_CATEGORY, "zxx-passthrough");
    BOOST_REQUIRE(spec != nullptr);

    auto *linguist = spec->as<wolf::LinguistSpec>();
    BOOST_CHECK_EQUAL(linguist->language(), "zxx");
    BOOST_CHECK_EQUAL(linguist->scheme(), "passthrough");

    // Load, unlike DataOnly, interprets exports through the selected provider.
    const auto *exports = spec->exports();
    BOOST_REQUIRE(exports != nullptr);
    const auto *linguistExports = exports->as<wolf::Api::Linguist::L1::LinguistExports>();
    BOOST_CHECK(linguistExports->phonemes.empty());
    // An empty list and an open set are two different statements. This language ends by handing
    // the word back, so the list is empty because it cannot be written, not because there is
    // nothing to write — and a host can now tell the two apart.
    BOOST_CHECK(linguistExports->openSet);

    // Both imports resolved and were given an execution factory, which is what the runtime later
    // walks to build the executive tree.
    BOOST_REQUIRE_EQUAL(spec->imports().size(), 2u);
    for (const auto &import : spec->imports()) {
        BOOST_CHECK_MESSAGE(import.binding() != nullptr, import.role() + " has no binding");
        BOOST_CHECK_MESSAGE(import.executiveFactory() != nullptr,
                            import.role() + " has no execution factory");
    }
}

/// The pair check is what stops a chain being assembled from members that do not agree on the
/// notation. Matching on contribution IDs, as the earlier design did, could not see this at all.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RejectsMismatchedLanguagePair) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("pair-mismatch"), srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!handle, "a chain member that declares another pair should be rejected");
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("cmn/pinyin") != std::string::npos, message);
}

/// Declaring no pairs forfeits the static match instead of failing, which is the only way a variant
/// whose output set is open or scripted can be declared honestly.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_AcceptsOmittedLanguages) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("pair-omitted"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("omitting exports.languages should still load: " + handle.error().toString());
    }
}

/// The flag defaults to false, so every declaration written before it existed still says exactly
/// what it said: this list is the whole of it.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_OpenSetDefaultsToClosed) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    // A fixture with no dependencies and no scripted module, so it loads in every build.
    auto handle = unit.openPackage(fixturePath("lang-chain"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the fixture should have loaded: " + handle.error().toString());
    }
    auto *spec = handle->contribution(wolf::LINGUIST_CATEGORY, "deu");
    BOOST_REQUIRE(spec != nullptr);
    const auto *exports = spec->exports()->as<wolf::Api::Linguist::L1::LinguistExports>();
    BOOST_REQUIRE(exports != nullptr);
    BOOST_CHECK(!exports->openSet);
}

/// A consumer pinned to an older packaging revision must still resolve against a newer one.
///
/// This is the whole point of giving every package a compatibility floor instead of letting
/// compatVersion equal version. With the floor at revision 0, a voicebank published against
/// revision 2 and never rebuilt keeps working when the pipeline emits revision 3; with
/// compatVersion equal to version, that voicebank stops resolving the day the packages are
/// repackaged, and the only symptom is "no installed Package satisfies dependency".
///
/// Deliberately built from contribution-free fixtures: the subject is version resolution, and the
/// real language packages are not present in every build.
/// The contract publishes a schema for exports, and the loader refuses what the schema refuses: a
/// key the contract does not define is a misspelling, and a misspelt field is a declaration that
/// quietly says less than it meant to.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesAnUnknownExportsKey) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("exports-unknown-key"), srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("phoneme") != std::string::npos, message);
}

/// Level 1 defines no import options, so an options object with anything in it is refused rather
/// than ignored: what an author wrote there was meant to do something.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesImportOptionsTheContractDoesNotDefine) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("options-nonempty"), srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("depth") != std::string::npos, message);
}

BOOST_AUTO_TEST_CASE(test_LinguistLoad_ServesAConsumerPinnedToAnOlderRevision) {
    srt::SynthUnit unit;
    configurePlugins(unit);
    std::vector<fs::path> packages = {fs::path(WOLF_TEST_FIXTURE_DIR)};
    unit.setPackagePaths(packages);

    auto handle = unit.openPackage(fixturePath("compat-consumer"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("a consumer pinned to revision 2 should be served by revision 3: " +
                   handle.error().toString());
    }

    // And the one it got is the newer package, not some copy of what it asked for.
    auto provider = unit.findLoadedPackages("wolf/test-compat-provider");
    BOOST_REQUIRE_EQUAL(provider.size(), 1u);
    BOOST_CHECK_EQUAL(provider.front().version().toString(), "1.0.0.3");
}

/// The mirror image, so the case above cannot pass by the loader ignoring the requested version.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesAConsumerPinnedAboveWhatIsInstalled) {
    srt::SynthUnit unit;
    configurePlugins(unit);
    std::vector<fs::path> packages = {fs::path(WOLF_TEST_FIXTURE_DIR)};
    unit.setPackagePaths(packages);

    auto handle = unit.openPackage(fixturePath("compat-consumer-too-new"), srt::SynthUnit::Load);
    BOOST_CHECK_MESSAGE(!handle, "revision 4 is not installed and must not resolve");
}

BOOST_AUTO_TEST_SUITE_END()
