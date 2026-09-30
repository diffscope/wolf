#ifndef WOLF_UTF8_H
#define WOLF_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wolf {

    /// Returns the length of the UTF-8 sequence that \a lead begins, from 1 to 4, or 0 if \a lead
    /// is a continuation byte or a byte that is invalid in UTF-8.
    ///
    /// This is the only function that interprets a lead byte. Whole strings are converted through
    /// stdc::utf. The two readers in this header iterate over a string one code point at a time
    /// and differ only in the handling of an invalid byte sequence, and they share only this
    /// function.
    std::size_t utf8SequenceLength(unsigned char lead) noexcept;

    /// Decodes the code point at \a text, reading at most \a available bytes.
    ///
    /// The decoder is strict. It returns the length of the sequence, or 0 if the bytes are not a
    /// complete, well-formed sequence, in which case \a code is unspecified. The Lua utf8 library
    /// reports such input to the script, and the decoder therefore never substitutes a guessed
    /// value.
    std::size_t decodeUtf8(const char *text, std::size_t available, std::uint32_t &code) noexcept;

    /// Splits \a text into code points.
    ///
    /// The splitter is lenient. A byte that begins no sequence becomes a separate element, and a
    /// sequence truncated by the end of the text takes the remaining bytes. No byte is dropped,
    /// so the concatenation of the elements equals \a text, and an invalid element fails every
    /// subsequent lookup.
    std::vector<std::string> splitUtf8(std::string_view text);

}

#endif // WOLF_UTF8_H
