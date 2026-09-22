#ifndef WOLF_INPUTRULES_H
#define WOLF_INPUTRULES_H

#include <string_view>

#include <wolf/wolf_global.h>

namespace wolf {

    /// What the contract's input validity ordering makes of one lyric.
    ///
    /// The chain inference contract states the ordering, first match wins: an empty or
    /// whitespace-only lyric is skipped, and any other lyric carrying whitespace is invalid
    /// input. Every G2P module owes this, so it is decided in one place — three copies of one
    /// rule drift, and this one had already drifted to zero copies.
    enum class LyricVerdict {
        Accept,  ///< A word to convert.
        Skip,    ///< Empty or whitespace only: mode is skip, with no pronunciation.
        Invalid, ///< Carries whitespace: the word passes through with InvalidInput.
    };

    WOLF_EXPORT LyricVerdict classifyLyric(std::string_view lyric) noexcept;

}

#endif // WOLF_INPUTRULES_H
