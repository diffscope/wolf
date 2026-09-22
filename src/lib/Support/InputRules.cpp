#include "InputRules.h"

namespace wolf {

    namespace {

        /// ASCII whitespace only. The delimiter this family reserves is the space, and treating
        /// some other code point as a separator would silently change how a pronunciation splits.
        bool isBlank(char character) noexcept {
            return character == ' ' || character == '\t' || character == '\n' ||
                   character == '\r' || character == '\v' || character == '\f';
        }

    }

    LyricVerdict classifyLyric(std::string_view lyric) noexcept {
        bool sawBlank = false;
        bool sawOther = false;
        for (const auto character : lyric) {
            if (isBlank(character)) {
                sawBlank = true;
            } else {
                sawOther = true;
            }
        }
        if (!sawOther) {
            return LyricVerdict::Skip;
        }
        return sawBlank ? LyricVerdict::Invalid : LyricVerdict::Accept;
    }

}
