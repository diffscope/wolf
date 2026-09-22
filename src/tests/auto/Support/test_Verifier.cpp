#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <synthrt/Support/JSON.h>

#include <wolf/Support/Verifier.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace {

    fs::path writeFile(const std::string &name, const std::string &content) {
        const auto path = fs::temp_directory_path() / ("wolf_verify_" + name);
        std::ofstream file(path);
        file << content;
        return path;
    }

    std::vector<wolf::VerifyEntry> parse(const std::string &json) {
        auto value = srt::JsonValue::fromJson(json, false, nullptr);
        auto entries = wolf::readVerifyEntries(value, "verify");
        BOOST_REQUIRE_MESSAGE(entries, "the entries should have parsed");
        return entries.take();
    }

    std::string rejection(const std::string &json) {
        auto value = srt::JsonValue::fromJson(json, false, nullptr);
        auto entries = wolf::readVerifyEntries(value, "verify");
        BOOST_REQUIRE_MESSAGE(!entries, "the entries should have been rejected: " + json);
        return entries.error().toString();
    }

}

BOOST_AUTO_TEST_SUITE(test_Verifier)

/// All three entry types have to work. The stack this is ported from compiled only the expression
/// type and dropped the other two without a word, so a declaration could silently do nothing.
BOOST_AUTO_TEST_CASE(test_Verifier_HonoursAllThreeTypes) {
    const auto dictionary = writeFile("words.txt", "alpha\tsomething\nbeta\tother\n\n");

    auto entries = parse(R"json([
        { "type": "regex", "value": ["([0-9]+)"], "mode": "copy" },
        { "type": "array", "value": ["beta"], "mode": "copy" },
        { "type": "dict",  "value": ["placeholder"], "mode": "convert" }
    ])json");
    entries[2].value[0] = dictionary.string();

    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE_MESSAGE(verifier, "the verifier should have been built");

    const auto modes = verifier->classify({"42", "alpha", "beta", "gamma"});
    BOOST_REQUIRE_EQUAL(modes.size(), 4u);
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Copy);
    // alpha and beta are both in the dictionary file, and that entry is declared last, so it wins
    // over the array entry that had marked beta copy.
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Convert);
    // Nothing matched, and an unmatched word copies.
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);

    fs::remove(dictionary);
}

/// Expressions must match the whole word, and several of them are one alternation.
BOOST_AUTO_TEST_CASE(test_Verifier_MatchesWholeWords) {
    auto entries = parse(R"json([
        { "type": "regex", "value": ["([A-Za-z]+)", "([0-9]+)"], "mode": "convert" }
    ])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(verifier);

    const auto modes = verifier->classify({"word", "123", "word123", ""});
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    // A partial match is not a match: the word is neither all letters nor all digits.
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Copy);
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);
}

/// The property classes the shipped language packages actually use need a real Unicode engine.
BOOST_AUTO_TEST_CASE(test_Verifier_UnderstandsUnicodeProperties) {
    auto entries = parse(R"json([
        { "type": "regex", "value": ["([\\p{Han}])+"], "mode": "convert" }
    ])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(verifier);

    const auto modes = verifier->classify({"\xE4\xB8\xAD", "\xE4\xB8\xAD\xE5\x9B\xBD", "abc"});
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Copy);
}

/// A malformed declaration fails the package. Each of these would otherwise be a rule that quietly
/// never fires.
BOOST_AUTO_TEST_CASE(test_Verifier_RejectsMalformedEntries) {
    BOOST_CHECK(rejection(R"json([{ "type": "glob", "value": ["a"], "mode": "copy" }])json")
                    .find("unknown") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "value": ["a"], "mode": "copy" }])json").find("type") !=
                std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "type": "array", "value": [], "mode": "copy" }])json")
                    .find("non-empty") != std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "type": "array", "value": ["a"] }])json").find("mode") !=
                std::string::npos);
    BOOST_CHECK(rejection(R"json([{ "type": "array", "value": ["a"], "mode": "skip" }])json")
                    .find("unknown mode") != std::string::npos);
    BOOST_CHECK(
        rejection(R"json([{ "type": "array", "value": ["a"], "mode": "copy", "tag": "x" }])json")
            .find("unknown key") != std::string::npos);

    // An unknown type is caught when the entries are read; a broken expression only when it is
    // compiled, which is still before any word reaches it.
    auto entries =
        parse(R"json([{ "type": "regex", "value": ["([unclosed"], "mode": "copy" }])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_CHECK(!verifier);

    // A dictionary entry naming a file that is not there fails too.
    auto missing =
        parse(R"json([{ "type": "dict", "value": ["nowhere.txt"], "mode": "copy" }])json");
    BOOST_CHECK(!wolf::Verifier::create(missing, fs::current_path()));
}

BOOST_AUTO_TEST_SUITE_END()
