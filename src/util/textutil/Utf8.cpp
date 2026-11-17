#include "Utf8.h"

#include <algorithm>

namespace wolf {

    std::size_t utf8SequenceLength(unsigned char lead) noexcept {
        if (lead < 0x80) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            // C0 and C1 can only begin a sequence that decodes below 0x80, which the shorter
            // encoding of that value already covers.
            return lead < 0xC2 ? 0 : 2;
        }
        if ((lead & 0xF0) == 0xE0) {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0) {
            // The four byte sequences above F4 exceed U+10FFFF, the last code point.
            return lead < 0xF5 ? 4 : 0;
        }
        return 0;
    }

    std::size_t decodeUtf8(const char *text, std::size_t available, std::uint32_t &code) noexcept {
        if (available == 0) {
            return 0;
        }
        const auto lead = static_cast<unsigned char>(text[0]);
        const auto length = utf8SequenceLength(lead);
        if (length == 0 || length > available) {
            return 0;
        }
        // The payload bits of the lead byte are those below its length prefix.
        static constexpr unsigned char LEAD_MASK[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
        code = lead & LEAD_MASK[length];
        for (std::size_t i = 1; i < length; ++i) {
            const auto continuation = static_cast<unsigned char>(text[i]);
            if ((continuation & 0xC0) != 0x80) {
                return 0;
            }
            code = (code << 6) | (continuation & 0x3FU);
        }
        // A sequence that decodes below the smallest value its length can encode is overlong,
        // and a surrogate or a value above U+10FFFF is not a code point.
        static constexpr std::uint32_t MINIMUM[] = {0, 0, 0x80, 0x800, 0x10000};
        if (code < MINIMUM[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            return 0;
        }
        return length;
    }

    std::vector<std::string> splitUtf8(std::string_view text) {
        std::vector<std::string> characters;
        for (std::size_t i = 0; i < text.size();) {
            auto length = utf8SequenceLength(static_cast<unsigned char>(text[i]));
            if (length == 0) {
                length = 1;
            }
            length = std::min(length, text.size() - i);
            characters.emplace_back(text.substr(i, length));
            i += length;
        }
        return characters;
    }

}
