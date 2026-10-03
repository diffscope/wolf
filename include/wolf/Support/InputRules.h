#ifndef WOLF_INPUTRULES_H
#define WOLF_INPUTRULES_H

#include <string_view>


namespace wolf {

    /// Classification of one lyric under the input validity ordering of the contract.
    ///
    /// The chain inference contract specifies the ordering, and the first match applies: an empty
    /// or whitespace-only lyric is skipped, and any other lyric that contains whitespace is
    /// invalid input. Every G2P module must apply this rule, and it is therefore implemented in
    /// one place, because separate copies of one rule diverge.
    enum class LyricVerdict {
        Accept,  ///< A word to convert.
        Skip,    ///< Empty or whitespace only: mode is skip, with no pronunciation.
        Invalid, ///< Contains whitespace: the word passes through with InvalidInput.
    };

    LyricVerdict classifyLyric(std::string_view lyric) noexcept;

}

#endif // WOLF_INPUTRULES_H
