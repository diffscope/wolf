#include <filesystem>
#include <fstream>
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

/// The published schema for exports.languages specifies the structure of a pair. The loader must
/// agree with the schema; otherwise a schema-invalid package could still load, and the schema
/// would be documentation rather than a contract.
BOOST_AUTO_TEST_CASE(test_ManifestValues_PairsMatchTheirGrammar) {
    auto read = wolf::readLanguageSchemes(parse(R"json([{ "language": "cmn", "scheme": "pinyin" },
                      { "language": "eng", "scheme": "arpabet-plus" }])json"),
                                          "exports languages");
    BOOST_REQUIRE(read);
    BOOST_REQUIRE_EQUAL(read->size(), 2u);
    BOOST_CHECK_EQUAL((*read)[1].scheme, "arpabet-plus");

    // A language that is not three lower-case letters can never match a linguist, whose identity
    // follows that grammar, so such a declaration has no reachable effect.
    BOOST_CHECK(rejection(R"json([{ "language": "english", "scheme": "arpabet" }])json")
                    .find("three lower case letters") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "ENG", "scheme": "arpabet" }])json")
                    .find("three lower case letters") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "ARPAbet" }])json")
                    .find("malformed scheme") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet-" }])json")
                    .find("malformed scheme") != std::string::npos);

    // Otherwise a misspelt key would leave the correct key absent and the pair silently
    // incomplete.
    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet", "note": "x" }])json")
                    .find("unknown key") != std::string::npos);

    BOOST_CHECK(rejection(R"json([{ "language": "eng", "scheme": "arpabet" },
                                  { "language": "eng", "scheme": "arpabet" }])json")
                    .find("unique") != std::string::npos);
}

/// A set may be written inline or as a path, and both forms are equivalent.
BOOST_AUTO_TEST_CASE(test_ManifestValues_StringSetsRejectEmptyAndRepeated) {
    auto read = wolf::readStringSet(parse(R"json(["a", "b"])json"), ".", "exports phonemes");
    BOOST_REQUIRE(read);
    BOOST_CHECK_EQUAL(read->size(), 2u);

    BOOST_CHECK(!wolf::readStringSet(parse(R"json(["a", ""])json"), ".", "exports phonemes"));
    BOOST_CHECK(!wolf::readStringSet(parse(R"json(["a", "a"])json"), ".", "exports phonemes"));
    BOOST_CHECK(!wolf::readStringSet(parse("42"), ".", "exports phonemes"));
}

/// Each of the two identity grammars is a single rule shared by the identity fields and by the
/// pairs inside exports.
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

/// Level 1 import options are an empty object. The loader substitutes an empty object for an
/// omitted key, so a null value can only come from the declaration itself. The contract, the
/// schema and the lint all reject it, and the loader must reject it too.
BOOST_AUTO_TEST_CASE(test_ManifestValues_ImportOptionsMustBeAnEmptyObject) {
    BOOST_CHECK(wolf::requireNoImportOptions(parse("{}"), "inference"));

    const auto null = wolf::requireNoImportOptions(parse("null"), "inference");
    BOOST_REQUIRE(!null);
    BOOST_CHECK(null.error().code() == srt::Error::InvalidFormat);

    BOOST_CHECK(!wolf::requireNoImportOptions(parse("[]"), "inference"));
    BOOST_CHECK(!wolf::requireNoImportOptions(parse(R"({"depth": 1})"), "inference"));
}

/// A string set written as a path follows the same rule as every other reader: a missing file
/// yields FileNotFound, an existing file that cannot be read yields FileNotOpen, and both messages
/// name the path. Regression: the two faults must not be collapsed into FileNotOpen.
BOOST_AUTO_TEST_CASE(test_ManifestValues_NamesTheStringSetFileItCannotRead) {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "wolf_manifest_values";
    fs::remove_all(base);
    fs::create_directories(base / "directory.json");

    auto missing = wolf::readStringSet(parse(R"("absent.json")"), base, "exports phonemes");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);
    BOOST_CHECK_MESSAGE(missing.error().message().find("absent.json") != std::string::npos,
                        missing.error().message());

    auto unreadable = wolf::readStringSet(parse(R"("directory.json")"), base, "exports phonemes");
    BOOST_REQUIRE(!unreadable);
    BOOST_CHECK(unreadable.error().code() == srt::Error::FileNotOpen);
    BOOST_CHECK_MESSAGE(unreadable.error().message().find("directory.json") != std::string::npos,
                        unreadable.error().message());
    fs::remove_all(base);
}

/// A declared path treats `\` as a separator, as the specification requires, so the same
/// declaration names the same file on a host that recognizes only `/` as a separator. On such a
/// host, a reader that passed the string to the file system unchanged would look for a single file
/// whose name contains backslashes.
BOOST_AUTO_TEST_CASE(test_ManifestValues_BackslashesSeparateDeclaredPaths) {
    namespace fs = std::filesystem;
    BOOST_CHECK(wolf::pathFromManifest("assets\\dict\\words.txt") ==
                fs::path("assets") / "dict" / "words.txt");
    BOOST_CHECK(wolf::pathFromManifest("assets\\dict/words.txt") ==
                fs::path("assets") / "dict" / "words.txt");
    BOOST_CHECK(wolf::pathFromManifest("words.txt") == fs::path("words.txt"));

    // A reader resolves such a path to the file that it names.
    const auto base = fs::temp_directory_path() / "wolf_manifest_backslash";
    fs::remove_all(base);
    fs::create_directories(base / "sub");
    {
        std::ofstream file(base / "sub" / "list.json");
        file << R"(["a", "b"])";
    }
    auto read = wolf::readStringSet(parse(R"("sub\\list.json")"), base, "exports phonemes");
    BOOST_REQUIRE_MESSAGE(read, wolf::test::why(read));
    BOOST_CHECK(*read == std::vector<std::string>({"a", "b"}));
    fs::remove_all(base);
}

BOOST_AUTO_TEST_SUITE_END()
