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
    /// Each line holds a word, a tab and a pronunciation. A word may appear more than once, and the
    /// readings of one word form its candidate group in file order. The loader treats the CMU
    /// convention of writing additional readings as word(2), word(3) as the same word rather than
    /// as separate entries, because no lookup ever uses those keys.
    class ChainDictionary {
    public:
        static srt::Expected<ChainDictionary> load(const std::filesystem::path &path);

        /// Returns the candidate group of \a word, or \c nullptr if the dictionary has no entry
        /// for \a word.
        const std::vector<std::string> *find(const std::string &word) const;

        std::size_t size() const noexcept;

    private:
        std::unordered_map<std::string, std::vector<std::string>> m_entries;
    };

    /// Converts every code point of a UTF-8 string to lowercase.
    ///
    /// The conversion covers the ranges that the shipped dictionaries require: ASCII, Latin-1
    /// Supplement, Latin Extended-A and Cyrillic. A code point outside these ranges is left
    /// unchanged.
    std::string toLowercase(const std::string &text);

    /// Removes trailing spaces, tabs, carriage returns and line feeds from a pronunciation.
    std::string stripTrailingSpace(const std::string &text);

    /// Inserts a space after every alphanumeric run that is directly followed by a character
    /// other than a space.
    std::string addSpaceBetweenPhones(const std::string &text);

}

#endif // WOLF_CHAINTABLES_H
