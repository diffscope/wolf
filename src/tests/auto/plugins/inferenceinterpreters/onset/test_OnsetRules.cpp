#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <onset/OnsetRules.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    struct TemporaryDirectory {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("class-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() {
            std::filesystem::create_directories(path);
        }
        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        std::filesystem::path write(const std::string &name, const std::string &text) {
            const auto result = path / name;
            std::ofstream stream(result, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
            return result;
        }
    };

}

BOOST_AUTO_TEST_CASE(longest_pattern_and_literal_priority) {
    TemporaryDirectory temporary;
    auto rules = wolf::onset::RuleTable::load(temporary.write(
        "rules",
        R"({"phonemeTypes":{"a":"vowel","b":"consonant"},"rules":[{"pattern":["*"],"onsets":[]},{"pattern":["consonant","vowel"],"onsets":[1]},{"pattern":["b","a"],"onsets":[0]}]})"));
    BOOST_REQUIRE(rules);
    BOOST_CHECK(rules.value().mark({"b", "a", "unknown"}) ==
                std::vector<bool>({true, false, false}));
    BOOST_CHECK(rules.value().mark({}).empty());
}

BOOST_AUTO_TEST_CASE(unsupported_version_precedes_unknown_keys) {
    TemporaryDirectory temporary;
    auto future = wolf::onset::RuleTable::load(
        temporary.write("future", R"({"formatVersion":2,"future":true})"));
    BOOST_REQUIRE(!future);
    BOOST_CHECK(future.error().code() == srt::Error::FeatureNotSupported);
    auto invalid = wolf::onset::RuleTable::load(temporary.write(
        "invalid", R"({"phonemeTypes":{"a":"vowel"},"rules":[{"pattern":["a"],"onsets":[1]}]})"));
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
}
