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

/// All three entry types must work. The original implementation from which this code was ported
/// compiled only the expression type and silently dropped the other two, so a declaration could
/// have no effect without any diagnostic.
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
    // alpha and beta are both in the dictionary file, and that entry is declared last, so it takes
    // precedence over the array entry that marked beta as Copy.
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Convert);
    // No entry matched, and an unmatched word is classified as Copy.
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);

    fs::remove(dictionary);
}

/// Expressions must match the whole word, and several expressions form one alternation.
BOOST_AUTO_TEST_CASE(test_Verifier_MatchesWholeWords) {
    auto entries = parse(R"json([
        { "type": "regex", "value": ["([A-Za-z]+)", "([0-9]+)"], "mode": "convert" }
    ])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(verifier);

    const auto modes = verifier->classify({"word", "123", "word123", ""});
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    // A partial match does not count: the word consists neither only of letters nor only of
    // digits.
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Copy);
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);
}

/// Each pattern of an entry keeps its own inline flags. A flag remains in effect until the end of
/// its enclosing group.
///
/// Regression: joining the patterns with a bare `|` must not apply the flags of the first pattern
/// to the second.
BOOST_AUTO_TEST_CASE(test_Verifier_KeepsInlineFlagsToTheirOwnPattern) {
    auto entries = parse(R"json([
        { "type": "regex", "value": ["(?i)abc", "XYZ"], "mode": "convert" }
    ])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(verifier);

    const auto modes = verifier->classify({"ABC", "abc", "XYZ", "xyz"});
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);
}

/// A pattern that contains an ungrouped alternation still matches only whole words.
BOOST_AUTO_TEST_CASE(test_Verifier_KeepsEachPatternsAlternationInside) {
    auto entries = parse(R"json([
        { "type": "regex", "value": ["a|b", "c"], "mode": "convert" }
    ])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(verifier);

    const auto modes = verifier->classify({"a", "b", "c", "ab", "bc"});
    BOOST_CHECK(modes[0] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[1] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[2] == wolf::VerifyMode::Convert);
    BOOST_CHECK(modes[3] == wolf::VerifyMode::Copy);
    BOOST_CHECK(modes[4] == wolf::VerifyMode::Copy);
}

/// A missing dictionary and an unreadable dictionary are different faults, distinguished by the
/// error code and identified by path.
///
/// Regression: the verifier must not report both as "not found", and must not accept a directory
/// in place of the file as an empty dictionary.
BOOST_AUTO_TEST_CASE(test_Verifier_SeparatesAMissingDictionaryFromAnUnreadableOne) {
    auto entries = parse(R"json([
        { "type": "dict", "value": ["placeholder"], "mode": "convert" }
    ])json");

    const auto missing = fs::temp_directory_path() / "wolf_verify_absent.txt";
    fs::remove_all(missing);
    entries[0].value[0] = missing.string();
    auto absent = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(!absent);
    BOOST_CHECK(absent.error().code() == srt::Error::FileNotFound);
    BOOST_CHECK_MESSAGE(absent.error().message().find("wolf_verify_absent.txt") !=
                            std::string::npos,
                        absent.error().message());

    const auto directory = fs::temp_directory_path() / "wolf_verify_directory";
    fs::remove_all(directory);
    fs::create_directory(directory);
    entries[0].value[0] = directory.string();
    auto unreadable = wolf::Verifier::create(entries, fs::current_path());
    BOOST_REQUIRE(!unreadable);
    BOOST_CHECK(unreadable.error().code() == srt::Error::FileNotOpen);
    BOOST_CHECK_MESSAGE(unreadable.error().message().find("wolf_verify_directory") !=
                            std::string::npos,
                        unreadable.error().message());
    fs::remove_all(directory);
}

/// The property classes used by the shipped language packages require a full Unicode regular
/// expression engine.
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

/// A malformed declaration fails the package. Otherwise, each of these declarations would be a
/// rule that silently never applies.
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

    // An unknown type is detected when the entries are read, and an invalid expression when it is
    // compiled, which still precedes any conversion.
    auto entries =
        parse(R"json([{ "type": "regex", "value": ["([unclosed"], "mode": "copy" }])json");
    auto verifier = wolf::Verifier::create(entries, fs::current_path());
    BOOST_CHECK(!verifier);

    // A dictionary entry that names a missing file also fails.
    auto missing =
        parse(R"json([{ "type": "dict", "value": ["nowhere.txt"], "mode": "copy" }])json");
    BOOST_CHECK(!wolf::Verifier::create(missing, fs::current_path()));
}

BOOST_AUTO_TEST_SUITE_END()
