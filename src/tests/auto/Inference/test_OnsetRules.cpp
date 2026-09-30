#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace {

    fs::path writePackage(const std::string &name, const std::string &rules) {
        const auto root = fs::temp_directory_path() / ("wolf-onset-" + name);
        fs::remove_all(root);
        fs::create_directories(root / "inferences" / "onset");

        std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-onset-)" << name << R"(",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": { "inference": [ { "id": "onset", "path": "./inferences/onset/inference.json" } ] }
})";
        std::ofstream(root / "inferences" / "onset" / "inference.json") << R"({
    "interface": "org.openvpi.wolf.inference.Onset",
    "level": 1,
    "variant": "rule",
    "configuration": { "file": "rules.json" }
})";
        std::ofstream(root / "inferences" / "onset" / "rules.json") << rules;
        return root;
    }

    srt::Expected<srt::PackageHandle> load(srt::SynthUnit &unit, const fs::path &package) {
        wolf::test::configure(unit, {});
        return unit.openPackage(package, srt::SynthUnit::Load);
    }

    std::vector<bool> mark(srt::PackageHandle &handle, const std::vector<std::string> &phonemes) {
        auto *spec = handle.contribution("inference", "onset")->as<srt::InferenceSpec>();
        OnsetApi::OnsetImportOptions options(spec->variant());
        OnsetApi::OnsetRuntimeOptions runtime(spec->variant());
        auto executive = spec->createInference(options, runtime);
        BOOST_REQUIRE(executive);

        OnsetApi::OnsetStartInput input;
        input.phonemes.push_back(phonemes);
        auto result = static_cast<OnsetApi::OnsetExecutive *>(executive->get())->start(input);
        BOOST_REQUIRE(result);
        auto onsets = (*result)->onsets.front();
        executive->reset();
        return onsets;
    }

    const char *TYPED_RULES = R"({
    "phonemeTypes": { "n": "consonant", "b": "consonant", "a": "vowel", "i": "vowel" },
    "rules": [
        { "pattern": ["consonant", "vowel"], "onsets": [0] },
        { "pattern": ["vowel"], "onsets": [0] }
    ]
})";

}

BOOST_AUTO_TEST_SUITE(test_OnsetRules)

/// The longer rule takes precedence, and the scan resumes after the matched segment.
BOOST_AUTO_TEST_CASE(test_OnsetRules_PrefersTheLongerMatch) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("typed", TYPED_RULES));
    if (!handle) {
        BOOST_FAIL("the rules should have loaded: " + handle.error().toString());
    }
    const auto onsets = mark(*handle, {"n", "a", "i"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 3u);
    BOOST_CHECK(onsets[0]); // the consonant-vowel rule matched and marked its first position
    BOOST_CHECK(!onsets[1]);
    BOOST_CHECK(onsets[2]); // the single-vowel rule then matched the remaining phoneme
}

/// A literal segment takes precedence over the type of its phoneme, which allows a single phoneme
/// to override the rule written for its whole class. The ported implementation handled this case
/// incorrectly: every segment entered the match tree as a wildcard, so a literal never matched.
BOOST_AUTO_TEST_CASE(test_OnsetRules_LiteralBeatsItsType) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("literal", R"({
    "phonemeTypes": { "n": "consonant", "a": "vowel" },
    "rules": [
        { "pattern": ["consonant", "vowel"], "onsets": [0] },
        { "pattern": ["n", "a"], "onsets": [1] }
    ]
})"));
    BOOST_REQUIRE(handle);
    const auto onsets = mark(*handle, {"n", "a"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 2u);
    BOOST_CHECK(!onsets[0]);
    BOOST_CHECK(onsets[1]);
}

/// A phoneme with no registered type matches only a literal segment or the wildcard, and a
/// position that no rule covers remains false rather than causing a failure.
BOOST_AUTO_TEST_CASE(test_OnsetRules_LeavesUncoveredPositionsFalse) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("uncovered", TYPED_RULES));
    BOOST_REQUIRE(handle);
    const auto onsets = mark(*handle, {"zzz", "a"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 2u);
    BOOST_CHECK(!onsets[0]);
    BOOST_CHECK(onsets[1]);
}

/// The wildcard matches any phoneme, including phonemes absent from the type table; a derived
/// recognition set is therefore a lower bound rather than the complete coverage.
BOOST_AUTO_TEST_CASE(test_OnsetRules_WildcardCoversUnregistered) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("wildcard", R"({
    "phonemeTypes": { "a": "vowel" },
    "rules": [ { "pattern": ["*"], "onsets": [0] } ]
})"));
    BOOST_REQUIRE(handle);
    const auto onsets = mark(*handle, {"qqq"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 1u);
    BOOST_CHECK(onsets[0]);
}

/// The version field is optional because no rule file shipped so far contains it, and a file that
/// specifies the current version must be read identically to a file without the field.
BOOST_AUTO_TEST_CASE(test_OnsetRules_AcceptsDeclaredVersion) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("versioned", R"({
    "formatVersion": 1,
    "phonemeTypes": { "a": "vowel" },
    "rules": [ { "pattern": ["a"], "onsets": [0] } ]
})"));
    BOOST_REQUIRE(handle);
    const auto onsets = mark(*handle, {"a"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 1u);
    BOOST_CHECK(onsets[0]);
}

BOOST_AUTO_TEST_CASE(test_OnsetRules_RejectsMalformedRules) {
    const struct {
        const char *name;
        const char *rules;
        const char *reason;
    } cases[] = {
        {"no-types",    R"({ "rules": [] })",                               "phonemeTypes"    },
        {"empty-types", R"({ "phonemeTypes": {}, "rules": [] })",           "phonemeTypes"    },
        {"star-type",   R"({ "phonemeTypes": { "a": "*" }, "rules": [] })", "may not be named"},
        {"no-rules",    R"({ "phonemeTypes": { "a": "vowel" } })",          "rules must be"   },
        {"bad-onset",   R"({ "phonemeTypes": { "a": "vowel" },
                             "rules": [ { "pattern": ["a"], "onsets": [3] } ] })",
         "outside its pattern"                                                                },
        {"extra-key",   R"({ "phonemeTypes": { "a": "vowel" }, "rules": [], "extra": 1 })",
         "unknown key in the rule file"                                                       },
        {"extra-rule-key", R"({ "phonemeTypes": { "a": "vowel" },
                                "rules": [ { "pattern": ["a"], "onsets": [0], "extra": 1 } ] })",
         "unknown key in a rule"                                                              },
        {"zero-version", R"({ "formatVersion": 0, "phonemeTypes": { "a": "vowel" },
                                                   "rules": [] })",
         "positive integer"                                                                   },
        {"future-version", R"({ "formatVersion": 99, "phonemeTypes": { "a": "vowel" },
                                                    "rules": [] })",
         "supports format versions up to"                                                     },
    };
    for (const auto &entry : cases) {
        srt::SynthUnit unit;
        auto handle = load(unit, writePackage(entry.name, entry.rules));
        BOOST_REQUIRE_MESSAGE(!handle, std::string("should have been rejected: ") + entry.name);
        const auto message = handle.error().toString();
        BOOST_CHECK_MESSAGE(message.find(entry.reason) != std::string::npos,
                            std::string(entry.name) + " failed for the wrong reason: " + message);
    }
}

/// A missing rule file and an unreadable rule file are different faults. Callers branch on the
/// error code, so the two faults must keep different codes.
///
/// Both cases fail in the loader that measures and hashes the files declared by a package. That
/// loader reports the error before any plugin reads the file, and its message does not name the
/// file. The loader in this plugin is reached through the other route, in which a voicebank
/// specifies its own rule file; the messages of these two faults are therefore checked in that
/// route rather than here.
BOOST_AUTO_TEST_CASE(test_OnsetRules_SeparatesAMissingFileFromAnUnreadableOne) {
    {
        srt::SynthUnit unit;
        const auto root = writePackage("absent", TYPED_RULES);
        fs::remove(root / "inferences" / "onset" / "rules.json");
        auto refused = load(unit, root);
        BOOST_REQUIRE(!refused);
        BOOST_CHECK(refused.error().code() == srt::Error::FileNotFound);
        fs::remove_all(root);
    }
    {
        // A path that exists but cannot be opened as a file produces the same open failure as a
        // locked file or a file with insufficient permissions.
        srt::SynthUnit unit;
        const auto root = writePackage("unreadable", TYPED_RULES);
        const auto rules = root / "inferences" / "onset" / "rules.json";
        fs::remove(rules);
        fs::create_directory(rules);
        auto refused = load(unit, root);
        BOOST_REQUIRE(!refused);
        BOOST_CHECK(refused.error().code() == srt::Error::FileNotOpen);
        fs::remove_all(root);
    }
}

BOOST_AUTO_TEST_SUITE_END()
