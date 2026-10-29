#ifndef WOLF_S2PTABLES_H
#define WOLF_S2PTABLES_H

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::s2p {

    /// Splits a pronunciation on ASCII spaces.
    ///
    /// Consecutive, leading and trailing spaces produce no element, so the output contains no empty
    /// phoneme. Tabs are not separators, because the dictionary formats use tabs as column
    /// separators.
    ///
    /// \return The phonemes in order; an empty sequence if \a pronunciation contains only
    /// spaces or is empty.
    std::vector<std::string> splitPronunciation(std::string_view pronunciation);

    /// A whole-pronunciation lookup table.
    ///
    /// Parsing is intentionally strict: a line without a tab, with more than one tab, with an empty
    /// column, without phonemes, or with a duplicate pronunciation causes the whole file to be
    /// rejected. A dictionary that silently skipped an unreadable line would produce incorrect
    /// conversions without a diagnostic.
    class DictionaryTable {
    public:
        static srt::Expected<DictionaryTable> load(const std::filesystem::path &path);

        /// Returns the phonemes for \a pronunciation.
        ///
        /// A missing entry is not a failure, because Level 1 provides S2P with no per-unit error
        /// channel.
        ///
        /// \return The phonemes of the entry; an empty sequence if \a pronunciation is absent.
        std::vector<std::string> convert(std::string_view pronunciation) const;

    private:
        std::map<std::string, std::vector<std::string>, std::less<>> m_entries;
    };

    /// A phoneme-by-phoneme substitution table.
    ///
    /// A phoneme that the table does not list passes through unchanged. The output set of this
    /// variant is therefore the target column together with every unlisted input phoneme.
    class MappingTable {
    public:
        static srt::Expected<MappingTable> load(const std::filesystem::path &path);

        std::vector<std::string> convert(std::string_view pronunciation) const;

    private:
        std::map<std::string, std::string, std::less<>> m_entries;
    };

}

#endif // WOLF_S2PTABLES_H
