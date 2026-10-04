#include <cstdint>
#include <string>
#include <string_view>

#include <wolf/Support/Utf8.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    /// Decodes the first code point of \a text and reports what the decoder returned.
    struct Decoded {
        std::size_t length = 0;
        std::uint32_t code = 0;
    };

    Decoded decode(std::string_view text) {
        Decoded decoded;
        decoded.length = wolf::decodeUtf8(text.data(), text.size(), decoded.code);
        return decoded;
    }

    /// Decodes \a text after the given number of leading bytes, so that a truncated sequence and a
    /// sequence at the end of a longer text are both covered.
    Decoded decodeAt(std::string_view text, std::size_t offset) {
        Decoded decoded;
        decoded.length = wolf::decodeUtf8(text.data() + offset, text.size() - offset, decoded.code);
        return decoded;
    }

    BOOST_AUTO_TEST_CASE(sequence_length_rejects_every_invalid_lead) {
        // Continuation bytes begin no sequence, C0 and C1 can only begin an overlong sequence, and
        // the leads above F4 encode values above the last code point.
        for (const auto lead : {0x80U, 0x9FU, 0xBFU, 0xC0U, 0xC1U, 0xF5U, 0xF7U, 0xF8U, 0xFFU}) {
            BOOST_TEST(wolf::utf8SequenceLength(static_cast<unsigned char>(lead)) == 0U);
        }
        BOOST_TEST(wolf::utf8SequenceLength(0x00U) == 1U);
        BOOST_TEST(wolf::utf8SequenceLength(0x7FU) == 1U);
        BOOST_TEST(wolf::utf8SequenceLength(0xC2U) == 2U);
        BOOST_TEST(wolf::utf8SequenceLength(0xDFU) == 2U);
        BOOST_TEST(wolf::utf8SequenceLength(0xE0U) == 3U);
        BOOST_TEST(wolf::utf8SequenceLength(0xEFU) == 3U);
        BOOST_TEST(wolf::utf8SequenceLength(0xF0U) == 4U);
        BOOST_TEST(wolf::utf8SequenceLength(0xF4U) == 4U);
    }

    BOOST_AUTO_TEST_CASE(decode_accepts_well_formed_sequences) {
        BOOST_TEST(decode("a").length == 1U);
        BOOST_TEST(decode("a").code == 0x61U);
        // The first and the last sequence of each length, and both sides of the surrogate range.
        BOOST_TEST(decode("\xC2\x80").code == 0x80U);
        BOOST_TEST(decode("\xDF\xBF").code == 0x7FFU);
        BOOST_TEST(decode("\xE0\xA0\x80").code == 0x800U);
        BOOST_TEST(decode("\xED\x9F\xBF").code == 0xD7FFU);
        BOOST_TEST(decode("\xEE\x80\x80").code == 0xE000U);
        BOOST_TEST(decode("\xF0\x90\x80\x80").code == 0x10000U);
        BOOST_TEST(decode("\xF4\x8F\xBF\xBF").code == 0x10FFFFU);
        BOOST_TEST(decode("\xF4\x8F\xBF\xBF").length == 4U);
    }

    BOOST_AUTO_TEST_CASE(decode_rejects_overlong_sequences) {
        // Every one of these decodes a value that a shorter sequence already covers.
        BOOST_TEST(decode("\xC0\x80").length == 0U);
        BOOST_TEST(decode("\xC1\xBF").length == 0U);
        BOOST_TEST(decode("\xE0\x80\x80").length == 0U);
        BOOST_TEST(decode("\xE0\x9F\xBF").length == 0U);
        BOOST_TEST(decode("\xF0\x80\x80\x80").length == 0U);
        BOOST_TEST(decode("\xF0\x8F\xBF\xBF").length == 0U);
    }

    BOOST_AUTO_TEST_CASE(decode_rejects_surrogates_and_values_above_the_last_code_point) {
        BOOST_TEST(decode("\xED\xA0\x80").length == 0U);
        BOOST_TEST(decode("\xED\xBF\xBF").length == 0U);
        BOOST_TEST(decode("\xF4\x90\x80\x80").length == 0U);
    }

    BOOST_AUTO_TEST_CASE(decode_rejects_incomplete_and_misplaced_continuations) {
        // A sequence cut short by the end of the text.
        BOOST_TEST(decode("\xE2").length == 0U);
        BOOST_TEST(decode("\xE2\x82").length == 0U);
        BOOST_TEST(decode("\xF0\x9F\x98").length == 0U);
        // A continuation byte where the sequence expects one.
        BOOST_TEST(decode("\xC3\x28").length == 0U);
        BOOST_TEST(decode("\xE2\x28\xA1").length == 0U);
        // A lead byte where the sequence expects a continuation.
        BOOST_TEST(decode("\xE2\x82\x41").length == 0U);
        // A continuation byte on its own.
        BOOST_TEST(decode("\x80").length == 0U);
        // A sequence at the end of a longer text is judged by the bytes that remain, not by the
        // bytes before it.
        BOOST_TEST(decodeAt("a\xC3\xA9", 1).length == 2U);
        BOOST_TEST(decodeAt("a\xC3", 1).length == 0U);
    }

    BOOST_AUTO_TEST_CASE(decode_rejects_an_empty_text_without_reading_it) {
        // An empty text has no first byte, and the decoder must return before it reads one: the Lua
        // iterator hands it the text after its last character, which is exactly this case.
        BOOST_TEST(decode("").length == 0U);
        const std::string_view text = "x";
        BOOST_TEST(decodeAt(text, 1).length == 0U);
    }

    BOOST_AUTO_TEST_CASE(splitter_drops_no_byte) {
        // The splitter is lenient on purpose: an invalid byte becomes an element of its own and a
        // truncated sequence takes what remains, so that the elements always reassemble the input.
        for (const std::string_view text : {"", "abc", "\xC3\xA9", "\xE2\x82", "a\x80z", "\xF0\x9F\x98"}) {
            std::string joined;
            for (const auto &character : wolf::splitUtf8(text)) {
                joined += character;
            }
            BOOST_TEST(joined == std::string(text));
        }
    }

}
