#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <chain/ChainTables.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

using wolf::chain::addSpaceBetweenPhones;
using wolf::chain::ChainDictionary;
using wolf::chain::stripTrailingSpace;
using wolf::chain::toLowercase;

namespace {

    /// Writes \a content to a scratch file byte for byte and returns its path.
    fs::path writeDictionary(const std::string &name, const std::string &content) {
        const auto path = fs::temp_directory_path() / ("wolf-chaintables-" + name + ".txt");
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }

    std::string loadError(const std::string &name, const std::string &content) {
        const auto path = writeDictionary(name, content);
        auto dictionary = ChainDictionary::load(path);
        fs::remove(path);
        BOOST_REQUIRE_MESSAGE(!dictionary, "the dictionary " + name + " should be refused");
        return dictionary.error().message();
    }

}

BOOST_AUTO_TEST_SUITE(test_ChainTables)

BOOST_AUTO_TEST_CASE(test_ChainTables_LowercasesAsciiAndLatin1) {
    BOOST_CHECK_EQUAL(toLowercase("HeLLo World 42"), "hello world 42");
    // A with grave to thorn, skipping the multiplication sign, which has no case
    BOOST_CHECK_EQUAL(toLowercase("\xC3\x80\xC3\x89\xC3\x96\xC3\x98\xC3\x9E"),
                      "\xC3\xA0\xC3\xA9\xC3\xB6\xC3\xB8\xC3\xBE");
    BOOST_CHECK_EQUAL(toLowercase("\xC3\x97"), "\xC3\x97");
    // Sharp s is a small letter
    BOOST_CHECK_EQUAL(toLowercase("\xC3\x9F"), "\xC3\x9F");
}

BOOST_AUTO_TEST_CASE(test_ChainTables_LowercasesCyrillic) {
    // Privet
    BOOST_CHECK_EQUAL(toLowercase("\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2"),
                      "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    // Io and Ukrainian Ie, from the run U+0400 to U+040F
    BOOST_CHECK_EQUAL(toLowercase("\xD0\x81\xD0\x84"), "\xD1\x91\xD1\x94");
    // Greek capital alpha is outside the covered ranges
    BOOST_CHECK_EQUAL(toLowercase("\xCE\x91"), "\xCE\x91");
}

BOOST_AUTO_TEST_CASE(test_ChainTables_StripsTrailingSpace) {
    BOOST_CHECK_EQUAL(stripTrailingSpace("hh ax l ow \t\r\n"), "hh ax l ow");
    BOOST_CHECK_EQUAL(stripTrailingSpace("  leading stays"), "  leading stays");
    BOOST_CHECK_EQUAL(stripTrailingSpace(" \t\r\n"), "");
    BOOST_CHECK_EQUAL(stripTrailingSpace(""), "");
}

BOOST_AUTO_TEST_CASE(test_ChainTables_SpacesPhonesApart) {
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones("hh,ax"), "hh ,ax");
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones("ah0-l"), "ah0 -l");
    // A run already followed by a space, and a mark at the start, are left alone
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones("hh ax"), "hh ax");
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones(",hh"), ",hh");
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones("a,,b"), "a ,,b");
    BOOST_CHECK_EQUAL(addSpaceBetweenPhones(""), "");
}

/// The upstream dictionaries carry a byte order mark and CRLF endings, and number additional
/// readings by the CMU convention. Only a parenthesised run of digits after a word is such a
/// number.
BOOST_AUTO_TEST_CASE(test_ChainTables_GroupsReadingsOfOneWord) {
    const auto path = writeDictionary("groups", "\xEF\xBB\xBFread\tr iy d\r\n"
                                                "read(2)\tr eh d\r\n"
                                                "\r\n"
                                                "a(12)\tax\r\n"
                                                "word(x)\tw er d\r\n"
                                                "ab()\tae b\r\n"
                                                "(2)\tt uw\r\n");
    auto dictionary = ChainDictionary::load(path);
    fs::remove(path);
    BOOST_REQUIRE_MESSAGE(dictionary.hasValue(),
                          (dictionary ? std::string() : dictionary.error().message()));

    const auto *read = dictionary->find("read");
    BOOST_REQUIRE(read != nullptr);
    const std::vector<std::string> readings = {"r iy d", "r eh d"};
    BOOST_CHECK_EQUAL_COLLECTIONS(read->begin(), read->end(), readings.begin(), readings.end());
    BOOST_CHECK(dictionary->find("read(2)") == nullptr);
    BOOST_CHECK(dictionary->find("a") != nullptr);

    // Not numbers of a reading: kept as words of their own
    BOOST_CHECK(dictionary->find("word(x)") != nullptr);
    BOOST_CHECK(dictionary->find("ab()") != nullptr);
    BOOST_CHECK(dictionary->find("(2)") != nullptr);
    BOOST_CHECK_EQUAL(dictionary->size(), 5u);
}

BOOST_AUTO_TEST_CASE(test_ChainTables_RefusesLinesItCannotRead) {
    const auto missing = loadError("missing-tab", "ok\tx\nbroken\n");
    BOOST_CHECK_MESSAGE(missing.find("line 2: missing tab separator") != std::string::npos,
                        missing);
    const auto multiple = loadError("multiple-tabs", "a\tb\tc\n");
    BOOST_CHECK_MESSAGE(multiple.find("line 1: multiple tab separators") != std::string::npos,
                        multiple);
    const auto first = loadError("empty-first", "\tx\n");
    BOOST_CHECK_MESSAGE(first.find("empty first column") != std::string::npos, first);
    const auto second = loadError("empty-second", "x\t\r\n");
    BOOST_CHECK_MESSAGE(second.find("empty second column") != std::string::npos, second);
    const auto empty = loadError("empty", "\r\n\r\n");
    BOOST_CHECK_MESSAGE(empty.find("holds no entries") != std::string::npos, empty);
}

BOOST_AUTO_TEST_SUITE_END()
