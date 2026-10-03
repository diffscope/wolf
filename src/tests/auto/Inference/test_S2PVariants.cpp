#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace S2PApi = wolf::Api::S2P::L1;

namespace {

    /// Writes a single-module package around an S2P declaration and returns its directory, so that
    /// the variant is exercised through the loader rather than through its tables directly.
    fs::path writePackage(const std::string &name, const std::string &variant,
                          const std::string &configuration, const std::string &resource = {}) {
        const auto root = fs::temp_directory_path() / ("wolf-s2p-" + name);
        fs::remove_all(root);
        fs::create_directories(root / "inferences" / "s2p");

        std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-s2p-)" << name << R"(",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": { "inference": [ { "id": "s2p", "path": "./inferences/s2p/inference.json" } ] }
})";
        std::ofstream(root / "inferences" / "s2p" / "inference.json") << R"({
    "interface": "org.openvpi.wolf.inference.S2P",
    "level": 1,
    "variant": ")" << variant << R"(",
    "configuration": )" << configuration << "\n}";
        if (!resource.empty()) {
            std::ofstream(root / "inferences" / "s2p" / "table.tsv", std::ios::binary) << resource;
        }
        return root;
    }

    srt::Expected<srt::PackageHandle> load(srt::SynthUnit &unit, const fs::path &package) {
        wolf::test::configure(unit, {});
        return unit.openPackage(package, srt::SynthUnit::Load);
    }

    std::vector<std::string> convert(srt::PackageHandle &handle, const std::string &pronunciation) {
        auto *spec = handle.contribution("inference", "s2p")->as<srt::InferenceSpec>();
        S2PApi::S2PImportOptions options(spec->variant());
        S2PApi::S2PRuntimeOptions runtime(spec->variant());
        auto executive = spec->createInference(options, runtime);
        BOOST_REQUIRE(executive);

        S2PApi::S2PStartInput input;
        input.pronunciations.push_back(pronunciation);
        auto result = static_cast<S2PApi::S2PExecutive *>(executive->get())->start(input);
        BOOST_REQUIRE(result);
        auto phonemes = (*result)->phonemes.front();
        executive->reset();
        return phonemes;
    }

}

BOOST_AUTO_TEST_SUITE(test_S2PVariants)

/// The direct variant requires no resource and drops the empty pieces produced by consecutive
/// spaces, so no empty phoneme reaches the output.
BOOST_AUTO_TEST_CASE(test_S2PVariants_DirectSplitsOnSpaces) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("direct", "direct", "{}"));
    if (!handle) {
        BOOST_FAIL("direct should have loaded: " + handle.error().toString());
    }
    const auto phonemes = convert(*handle, "  n   i  ");
    BOOST_REQUIRE_EQUAL(phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(phonemes[0], "n");
    BOOST_CHECK_EQUAL(phonemes[1], "i");
}

/// The dict variant looks up the whole pronunciation. A miss yields no phonemes and is not a
/// failure, because Level 1 provides S2P with no per-unit error channel.
BOOST_AUTO_TEST_CASE(test_S2PVariants_DictLooksUpWholePronunciation) {
    srt::SynthUnit unit;
    auto handle = load(
        unit, writePackage("dict", "dict", R"({ "file": "table.tsv" })", "ni\tn i\nhao\th ao\n"));
    if (!handle) {
        BOOST_FAIL("dict should have loaded: " + handle.error().toString());
    }
    const auto hit = convert(*handle, "ni");
    BOOST_REQUIRE_EQUAL(hit.size(), 2u);
    BOOST_CHECK_EQUAL(hit[1], "i");
    BOOST_CHECK(convert(*handle, "absent").empty());
}

/// The mapping variant substitutes each phoneme individually and passes through unlisted phonemes;
/// its output set is therefore the target column together with every unlisted input phoneme.
BOOST_AUTO_TEST_CASE(test_S2PVariants_MappingPassesThroughUnlisted) {
    srt::SynthUnit unit;
    auto handle =
        load(unit, writePackage("mapping", "mapping", R"({ "file": "table.tsv" })", "a\tA\n"));
    BOOST_REQUIRE(handle);
    const auto phonemes = convert(*handle, "a b");
    BOOST_REQUIRE_EQUAL(phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(phonemes[0], "A");
    BOOST_CHECK_EQUAL(phonemes[1], "b");
}

/// Parsing is deliberately strict. A dictionary that silently skipped an unparsable line would
/// produce incorrect conversions without any error, so any malformed line rejects the whole file
/// at load time.
BOOST_AUTO_TEST_CASE(test_S2PVariants_RejectsMalformedTables) {
    const struct {
        const char *name;
        const char *content;
        const char *reason;
    } cases[] = {
        {"no-tab",    "ni n i\n",           "missing tab" },
        {"two-tabs",  "ni\tn\ti\n",         "multiple tab"},
        {"empty-key", "\tn i\n",            "empty first" },
        {"empty-val", "ni\t\n",             "empty second"},
        {"duplicate", "ni\tn i\nni\tn i\n", "duplicate"   },
    };
    for (const auto &entry : cases) {
        srt::SynthUnit unit;
        auto handle = load(
            unit, writePackage(entry.name, "dict", R"({ "file": "table.tsv" })", entry.content));
        BOOST_REQUIRE_MESSAGE(!handle, std::string("should have been rejected: ") + entry.name);
        const auto message = handle.error().toString();
        BOOST_CHECK_MESSAGE(message.find(entry.reason) != std::string::npos,
                            std::string(entry.name) + " failed for the wrong reason: " + message);
    }
}

/// A misspelled key is rejected rather than ignored, so it cannot silently leave a default in
/// place.
BOOST_AUTO_TEST_CASE(test_S2PVariants_RejectsUnknownConfigurationKey) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("unknown-key", "direct", R"({ "flie": "table.tsv" })"));
    BOOST_REQUIRE(!handle);
    BOOST_CHECK(handle.error().toString().find("unknown S2P configuration key") !=
                std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
