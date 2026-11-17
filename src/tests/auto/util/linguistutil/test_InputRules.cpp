#include <string>

#include <linguistutil/InputRules.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(ascii_whitespace_and_utf8_lyrics) {
    using wolf::LyricVerdict;
    BOOST_CHECK(wolf::classifyLyric("") == LyricVerdict::Skip);
    for (const char blank : std::string(" \t\r\n\v\f")) {
        const std::string space(1, blank);
        BOOST_CHECK(wolf::classifyLyric(space) == LyricVerdict::Skip);
        BOOST_CHECK(wolf::classifyLyric("a" + space) == LyricVerdict::Invalid);
        BOOST_CHECK(wolf::classifyLyric(space + "a") == LyricVerdict::Invalid);
        BOOST_CHECK(wolf::classifyLyric("a" + space + "b") == LyricVerdict::Invalid);
    }
    BOOST_CHECK(wolf::classifyLyric("hello") == LyricVerdict::Accept);
    BOOST_CHECK(wolf::classifyLyric(u8"你好") == LyricVerdict::Accept);
    BOOST_CHECK(wolf::classifyLyric(u8"a\u00a0b") == LyricVerdict::Accept);
}
