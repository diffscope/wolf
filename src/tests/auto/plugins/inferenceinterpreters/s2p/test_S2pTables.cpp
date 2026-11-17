#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <s2p/S2pTables.h>

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

BOOST_AUTO_TEST_CASE(dictionary_and_mapping_keep_their_distinct_fallbacks) {
    TemporaryDirectory temporary;
    auto dictionary =
        wolf::s2p::DictionaryTable::load(temporary.write("dict", "\xef\xbb\xbf"
                                                                 "ni\tn i\r\nhao\th ao\r\n"));
    BOOST_REQUIRE(dictionary);
    BOOST_CHECK(dictionary.value().convert("ni") == std::vector<std::string>({"n", "i"}));
    BOOST_CHECK(dictionary.value().convert("missing").empty());
    auto mapping = wolf::s2p::MappingTable::load(temporary.write("map", "n\tN\ni\tI\n"));
    BOOST_REQUIRE(mapping);
    BOOST_CHECK(mapping.value().convert(" n  i unknown ") ==
                std::vector<std::string>({"N", "I", "unknown"}));
    BOOST_CHECK(wolf::s2p::splitPronunciation(" a  b ") == std::vector<std::string>({"a", "b"}));
    BOOST_CHECK(wolf::s2p::splitPronunciation("a\tb") == std::vector<std::string>({"a\tb"}));
}

BOOST_AUTO_TEST_CASE(malformed_tables_fail_as_a_whole) {
    TemporaryDirectory temporary;
    int index = 0;
    for (const auto &content : {"no-tab\n", "x\ta\nx\tb\n", "x\t \n", "x\ta\textra\n", "\ta\n"}) {
        const auto path = temporary.write(std::to_string(index++), content);
        auto dictionary = wolf::s2p::DictionaryTable::load(path);
        BOOST_REQUIRE(!dictionary);
        BOOST_CHECK(dictionary.error().code() == srt::Error::InvalidFormat);
    }
}
