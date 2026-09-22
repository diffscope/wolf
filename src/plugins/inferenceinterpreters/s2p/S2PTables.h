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
    /// Runs of spaces and leading or trailing ones produce nothing, so no empty phoneme ever
    /// reaches the output. Tabs are not separators here; the dictionary formats use them to
    /// separate columns.
    std::vector<std::string> splitPronunciation(std::string_view pronunciation);

    /// A whole-pronunciation lookup table.
    ///
    /// Parsing is strict, and deliberately so: a line without a tab, with more than one, with an
    /// empty column, or repeating a pronunciation already seen rejects the whole file. A
    /// dictionary that silently drops the line it could not read would convert quietly and wrongly.
    class DictionaryTable {
    public:
        static srt::Expected<DictionaryTable> load(const std::filesystem::path &path);

        /// Returns the phonemes for \a pronunciation, or an empty sequence when it is absent.
        /// Missing is not a failure: Level 1 gives S2P no per unit error channel.
        std::vector<std::string> convert(std::string_view pronunciation) const;

    private:
        std::map<std::string, std::vector<std::string>, std::less<>> m_entries;
    };

    /// A phoneme-by-phoneme substitution table.
    ///
    /// A phoneme the table does not list passes through unchanged, which is what makes the output
    /// set of this variant the target column together with everything it did not touch.
    class MappingTable {
    public:
        static srt::Expected<MappingTable> load(const std::filesystem::path &path);

        std::vector<std::string> convert(std::string_view pronunciation) const;

        /// The target column, for the export that declares what this module can produce.
        std::vector<std::string> targets() const;

    private:
        std::map<std::string, std::string, std::less<>> m_entries;
    };

}

#endif // WOLF_S2PTABLES_H
