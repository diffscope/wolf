#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Support/JSON.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace {

    /// Sets the plugin search paths of \a unit for both plugin categories.
    ///
    /// The linguist category belongs to wolf and the inference category to synthrt, so the two
    /// categories have separate search paths even though both plugins are built in this repository.
    /// A search path contains one subdirectory per plugin; these paths are therefore the parent
    /// directories of the plugin directories.
    void configurePlugins(srt::SynthUnit &unit) {
        std::vector<fs::path> linguist = {fs::path(WOLF_TEST_LINGUIST_PLUGIN_DIR)};
        std::vector<fs::path> inference = {fs::path(WOLF_TEST_INFERENCE_PLUGIN_DIR)};
        unit.setPluginPaths(wolf::LINGUIST_CATEGORY, linguist);
        unit.setPluginPaths("inference", inference);
    }

    fs::path packagePath(const char *name) {
        return wolf::test::fixtureRoot() / name;
    }

    fs::path fixturePath(const char *name) {
        return wolf::test::fixtureRoot() / name;
    }

}

BOOST_AUTO_TEST_SUITE(test_LinguistLoad)

/// Loading wolf/lang-zxx end to end verifies that the contract family belongs to the built-in
/// inference category of synthrt: the loader must discover the stub plugin through the IID and the
/// search path of that category, select all three triples, and return the results as typed exports.
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
    // An empty list and an open set are distinct statements. This language returns the word
    // unchanged, so the list is empty because the output set cannot be enumerated, not because the
    // output set is empty. The openSet flag allows a host to distinguish the two cases.
    BOOST_CHECK(linguistExports->openSet);

    // Both imports resolved and received an execution factory. The runtime later traverses these
    // factories to build the executive tree.
    BOOST_REQUIRE_EQUAL(spec->imports().size(), 2u);
    for (const auto &import : spec->imports()) {
        BOOST_CHECK_MESSAGE(import.binding() != nullptr, import.role() + " has no binding");
        BOOST_CHECK_MESSAGE(import.executiveFactory() != nullptr,
                            import.role() + " has no execution factory");
    }
}

/// The pair check prevents a chain from being assembled from members that disagree on the notation.
/// Matching on contribution IDs, as in the earlier design, could not detect this mismatch.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RejectsMismatchedLanguagePair) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("pair-mismatch"), srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!handle, "a chain member that declares another pair should be rejected");
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("cmn/pinyin") != std::string::npos, message);
}

/// Declaring no pairs forgoes the static match instead of failing. Omission is the only accurate
/// declaration for a variant whose output set is open or produced by a script.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_AcceptsOmittedLanguages) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("pair-omitted"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("omitting exports.languages should still load: " + handle.error().toString());
    }
}

/// The flag defaults to false, so every declaration written before the flag existed retains its
/// meaning: the phoneme list is complete.
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

/// The contract publishes a schema for exports, and the loader rejects every key that the schema
/// rejects. A key that the contract does not define is a misspelling, and a declaration with a
/// misspelt field states less than its author intended.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesAnUnknownExportsKey) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("exports-unknown-key"), srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("phoneme") != std::string::npos, message);
}

/// Level 1 defines no import options, so a non-empty options object is rejected rather than
/// ignored, because the author intended its content to take effect.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesImportOptionsTheContractDoesNotDefine) {
    srt::SynthUnit unit;
    configurePlugins(unit);

    auto handle = unit.openPackage(fixturePath("options-nonempty"), srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("depth") != std::string::npos, message);
}

/// Checks that a consumer pinned to an older packaging revision still resolves against a newer
/// revision.
///
/// This behavior is the purpose of giving every package a compatibility floor instead of setting
/// compatVersion equal to version. With the floor at revision 0, a voicebank published against
/// revision 2 and never rebuilt keeps working when the pipeline emits revision 3. With
/// compatVersion equal to version, that voicebank stops resolving as soon as the packages are
/// repackaged, and the only symptom is "no installed Package satisfies dependency".
///
/// The case deliberately uses contribution-free fixtures because the subject is version
/// resolution, and the real language packages are not present in every build.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_ServesAConsumerPinnedToAnOlderRevision) {
    srt::SynthUnit unit;
    configurePlugins(unit);
    std::vector<fs::path> packages = {wolf::test::fixtureRoot()};
    unit.setPackagePaths(packages);

    auto handle = unit.openPackage(fixturePath("compat-consumer"), srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("a consumer pinned to revision 2 should be served by revision 3: " +
                   handle.error().toString());
    }

    // The resolved provider is the installed revision 3, not a package at the requested revision.
    auto provider = unit.findLoadedPackages("wolf/test-compat-provider");
    BOOST_REQUIRE_EQUAL(provider.size(), 1u);
    BOOST_CHECK_EQUAL(provider.front().version().toString(), "1.0.0.3");
}

/// Checks the converse case, so that the case above cannot pass because the loader ignores the
/// requested version.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_RefusesAConsumerPinnedAboveWhatIsInstalled) {
    srt::SynthUnit unit;
    configurePlugins(unit);
    std::vector<fs::path> packages = {wolf::test::fixtureRoot()};
    unit.setPackagePaths(packages);

    auto handle = unit.openPackage(fixturePath("compat-consumer-too-new"), srt::SynthUnit::Load);
    BOOST_CHECK_MESSAGE(!handle, "revision 4 is not installed and must not resolve");
}

/// Checks that every test package receives the verdict recorded for it in loader-verdicts.json and
/// that every test package is recorded there.
///
/// The lint self-test reads the same file and requires the lint to reject every package that the
/// loader rejects, except the packages listed with a reason. Checking the file against the loader
/// keeps that comparison valid, because a verdict that diverged from the loader would render the
/// lint test meaningless.
BOOST_AUTO_TEST_CASE(test_LinguistLoad_VerdictsMatchTheRecord) {
    // The record is tracked next to the sources rather than shipped with the generated packages.
    const auto recordPath = std::filesystem::path(WOLF_TEST_VERDICTS_FILE);
    std::ifstream file(recordPath);
    BOOST_REQUIRE_MESSAGE(file.is_open(), "cannot open " + recordPath.string());
    std::ostringstream text;
    text << file.rdbuf();
    stdc::json::ParseError error;
    const auto record = srt::JsonValue::fromJson(text.str(), true, &error);
    BOOST_REQUIRE_MESSAGE(!error && record.isObject(), "the record is not a JSON object");

    struct Expectation {
        bool loads = false;
        std::string requires;
    };
    std::map<std::string, Expectation> expected;
    const auto collect = [&](const char *group, bool loads) {
        const auto it = record.toObject().find(group);
        BOOST_REQUIRE_MESSAGE(it != record.toObject().end() && it->second.isObject(),
                              std::string("the record has no ") + group + " object");
        for (const auto &[name, details] : it->second.toObject()) {
            Expectation expectation;
            expectation.loads = loads;
            if (details.isObject()) {
                const auto requires = details.toObject().find("requires");
                if (requires != details.toObject().end()) {
                    expectation.requires = requires->second.toString();
                }
            }
            BOOST_CHECK_MESSAGE(expected.emplace(name, expectation).second,
                                name + " is recorded more than once");
        }
    };
    collect("refused", false);
    collect("refusedByTheLoaderOnly", false);
    collect("accepted", true);

    std::set<std::string> present;
    for (const auto &entry : fs::directory_iterator(wolf::test::fixtureRoot())) {
        if (fs::is_regular_file(entry.path() / "desc.json")) {
            present.insert(entry.path().filename().string());
        }
    }
    for (const auto &name : present) {
        BOOST_CHECK_MESSAGE(expected.count(name) != 0,
                            name + " is not recorded in loader-verdicts.json");
    }

    for (const auto &[name, expectation] : expected) {
        BOOST_CHECK_MESSAGE(present.count(name) != 0,
                            name + " is recorded but there is no such test package");
        if (present.count(name) == 0) {
            continue;
        }
#ifndef WOLF_TEST_HAS_LUAJIT
        if (expectation.requires == "luajit") {
            continue;
        }
#endif
        srt::SynthUnit unit;
        wolf::test::configure(unit,
                              {wolf::test::fixtureRoot()});
        auto handle = unit.openPackage(fixturePath(name.c_str()), srt::SynthUnit::Load);
        if (expectation.loads) {
            BOOST_CHECK_MESSAGE(handle, name + " should load: " + wolf::test::why(handle));
        } else {
            BOOST_CHECK_MESSAGE(!handle, name + " should be rejected by the loader");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
