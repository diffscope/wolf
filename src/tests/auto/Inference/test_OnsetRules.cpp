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

/// The longer rule wins, and the scan resumes past what it consumed.
BOOST_AUTO_TEST_CASE(test_OnsetRules_PrefersTheLongerMatch) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("typed", TYPED_RULES));
    if (!handle) {
        BOOST_FAIL("the rules should have loaded: " + handle.error().toString());
    }
    const auto onsets = mark(*handle, {"n", "a", "i"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 3u);
    BOOST_CHECK(onsets[0]); // consonant vowel matched, marking its first position
    BOOST_CHECK(!onsets[1]);
    BOOST_CHECK(onsets[2]); // the lone vowel rule then matched what was left
}

/// A literal segment beats the type its phoneme belongs to, which is what lets one phoneme
/// override the rule written for its whole class. This is the case the ported implementation got
/// wrong: every segment entered its match tree as a wildcard, so a literal never matched at all.
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
/// position no rule covers stays false rather than failing.
BOOST_AUTO_TEST_CASE(test_OnsetRules_LeavesUncoveredPositionsFalse) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("uncovered", TYPED_RULES));
    BOOST_REQUIRE(handle);
    const auto onsets = mark(*handle, {"zzz", "a"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 2u);
    BOOST_CHECK(!onsets[0]);
    BOOST_CHECK(onsets[1]);
}

/// The wildcard covers anything, including phonemes the type table never registered, which is why
/// a derived recognition set is a lower bound rather than the whole coverage.
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

/// The version field is optional, because every rule file shipped so far leaves it out, and a
/// file that names the current version has to read exactly like one that names nothing.
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
         "above the"                                                                          },
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

/// A rule file that is not there and one that cannot be read are different faults, and the code is
/// what a caller switches on, so the two have to keep different ones.
///
/// Both cases go out through the loader that sizes and hashes what a package declares, which
/// answers before any plugin sees the file — and answers without naming it. The loader in this
/// plugin is reached on the other route, where a voicebank names its own rule file, so the messages
/// these two faults carry are checked there rather than here.
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
        // A path that exists and cannot be read as a file is the shape a locked or mispermissioned
        // file presents to an open call, which reports the same failure as a missing one.
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
