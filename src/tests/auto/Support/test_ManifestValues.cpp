#include <string>
#include <vector>

#include <synthrt/Support/JSON.h>

#include <wolf/Support/ManifestValues.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace {

    srt::JsonValue parse(const std::string &json) {
        stdc::json::ParseError error;
        auto value = srt::JsonValue::fromJson(json, false, &error);
        BOOST_REQUIRE_MESSAGE(!error, "the test input should be valid JSON: " + error.message());
        return value;
    }

    std::string rejection(const std::string &json) {
        auto read = wolf::readLanguageSchemes(parse(json), "exports languages");
        BOOST_REQUIRE_MESSAGE(!read, "should have been rejected: " + json);
        return read.error().toString();
    }

}

BOOST_AUTO_TEST_SUITE(test_ManifestValues)

/// The published schema for exports.languages says what a pair looks like. The loader has to
/// agree with it, or a package can be schema-invalid and still load — which would make the
/// schema a decoration rather than a contract.
BOOST_AUTO_TEST_CASE(test_ManifestValues_PairsMatchTheirGrammar) {
    auto read = wolf::readLanguageSchemes(parse(R"json([{ "language": "cmn", "scheme": "pinyin" },
                      { "language": "eng", "scheme": "arpabet-plus" }])json"),
                                          "exports languages");
    BOOST_REQUIRE(read);
    BOOST_REQUIRE_EQUAL(read->size(), 2u);
    BOOST_CHECK_EQUAL((*read)[1].scheme, "arpabet-plus");

    // A language that is not three lower case letters can never match a linguist, whose own
    // identity answers to that grammar, so declaring one is a statement with no reachable effect.
    BOOST_CHECK(rejection(R"json([{ "language": "english", "scheme": "arpabet" }])json")
                    .find("three lower case letters") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "ENG", "scheme": "arpabet" }])json")
                    .find("three lower case letters") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "ARPAbet" }])json")
                    .find("malformed scheme") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet-" }])json")
                    .find("malformed scheme") != std::string::npos);

    // A misspelt key would otherwise leave the real one absent and the pair silently incomplete.
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet", "note": "x" }])json")
                    .find("unknown key") != std::string::npos);

    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet" },
                                  { "language": "eng", "scheme": "arpabet" }])json")
                    .find("unique") != std::string::npos);
}

/// A set may be written inline or as a path, and both forms mean the same thing.
BOOST_AUTO_TEST_CASE(test_ManifestValues_StringSetsRejectEmptyAndRepeated) {
    auto read = wolf::readStringSet(parse(R"json(["a", "b"])json"), ".", "exports phonemes");
    BOOST_REQUIRE(read);
    BOOST_CHECK_EQUAL(read->size(), 2u);

    BOOST_CHECK(!wolf::readStringSet(parse(R"json(["a", ""])json"), ".", "exports phonemes"));
    BOOST_CHECK(!wolf::readStringSet(parse(R"json(["a", "a"])json"), ".", "exports phonemes"));
    BOOST_CHECK(!wolf::readStringSet(parse("42"), ".", "exports phonemes"));
}

/// The two identity grammars are one rule each, shared by the identity fields and by the pairs
/// inside exports.
BOOST_AUTO_TEST_CASE(test_ManifestValues_IdentityGrammars) {
    BOOST_CHECK(wolf::isLanguageHandle("cmn"));
    BOOST_CHECK(wolf::isLanguageHandle("qaa"));
    BOOST_CHECK(!wolf::isLanguageHandle("cm"));
    BOOST_CHECK(!wolf::isLanguageHandle("cmns"));
    BOOST_CHECK(!wolf::isLanguageHandle("Cmn"));

    BOOST_CHECK(wolf::isSchemeName("pinyin"));
    BOOST_CHECK(wolf::isSchemeName("xsampa-geminate"));
    BOOST_CHECK(wolf::isSchemeName("ds2"));
    BOOST_CHECK(!wolf::isSchemeName(""));
    BOOST_CHECK(!wolf::isSchemeName("-pinyin"));
    BOOST_CHECK(!wolf::isSchemeName("pinyin-"));
    BOOST_CHECK(!wolf::isSchemeName("pin--yin"));
    BOOST_CHECK(!wolf::isSchemeName("pin_yin"));
}

BOOST_AUTO_TEST_SUITE_END()
