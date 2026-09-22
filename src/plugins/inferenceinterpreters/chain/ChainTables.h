#ifndef WOLF_CHAINTABLES_H
#define WOLF_CHAINTABLES_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::chain {

    /// A pronunciation dictionary read by the dict step.
    ///
    /// Lines are word, tab, pronunciation. A word may appear more than once, and the readings of
    /// one word form its candidate group in file order. The CMU convention of writing further
    /// readings as word(2), word(3) is understood as the same word rather than as separate
    /// entries, because nothing would ever look those keys up.
    class ChainDictionary {
    public:
        static srt::Expected<ChainDictionary> load(const std::filesystem::path &path);

        /// Returns the candidate group of \a word, or null when it has none.
        const std::vector<std::string> *find(const std::string &word) const;

        std::size_t size() const noexcept;

    private:
        std::unordered_map<std::string, std::vector<std::string>> m_entries;
    };

    /// Lower cases every code point of a UTF-8 string.
    ///
    /// Coverage is the ranges the shipped dictionaries need: ASCII, Latin-1 Supplement, Latin
    /// Extended-A and Cyrillic. A code point outside them is left alone.
    std::string toLowercase(const std::string &text);

    /// Drops trailing blanks from a pronunciation.
    std::string stripTrailingSpace(const std::string &text);

    /// Inserts a space wherever an alphanumeric run meets a symbol.
    std::string addSpaceBetweenPhones(const std::string &text);

}

#endif // WOLF_CHAINTABLES_H
