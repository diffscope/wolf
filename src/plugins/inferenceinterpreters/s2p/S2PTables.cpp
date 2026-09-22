#include "S2PTables.h"

#include <fstream>
#include <set>
#include <utility>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf::s2p {

    namespace {

        srt::Error parseError(const fs::path &path, std::size_t line, const std::string &what) {
            return srt::Error(srt::Error::InvalidFormat, stdc::path::to_utf8(path) + " line " +
                                                             std::to_string(line) + ": " + what);
        }

        /// Tells a table that is not there from one that is there and cannot be read.
        ///
        /// An open call that fails says only that it failed, and the two faults call for different
        /// answers: a missing table is a package that was installed wrong, an unreadable one is a
        /// host problem. The code is what a caller switches on, so it is decided by looking at the
        /// path rather than assumed from the failure.
        srt::Error fileError(const fs::path &path, const std::string &what) {
            std::error_code status;
            if (!fs::exists(path, status)) {
                return srt::Error(srt::Error::FileNotFound,
                                  what + " not found: " + stdc::path::to_utf8(path));
            }
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to open the " + what + ": " + stdc::path::to_utf8(path));
        }

        /// Splits one TSV line into exactly two columns, rejecting every other shape.
        srt::Expected<std::pair<std::string_view, std::string_view>>
            twoColumns(const fs::path &path, std::size_t number, std::string_view line) {
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos) {
                return parseError(path, number, "missing tab separator");
            }
            if (line.find('\t', tab + 1) != std::string_view::npos) {
                return parseError(path, number, "multiple tab separators");
            }
            if (tab == 0) {
                return parseError(path, number, "empty first column");
            }
            if (tab + 1 == line.size()) {
                return parseError(path, number, "empty second column");
            }
            return std::make_pair(line.substr(0, tab), line.substr(tab + 1));
        }

        /// Strips a UTF-8 byte order mark, which the upstream dictionaries carry.
        void stripBom(std::string &line, bool first) {
            if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) {
                line.erase(0, 3);
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
        }

    }

    std::vector<std::string> splitPronunciation(std::string_view pronunciation) {
        std::vector<std::string> phonemes;
        std::size_t begin = 0;
        while (begin < pronunciation.size()) {
            const auto end = pronunciation.find(' ', begin);
            const auto piece = pronunciation.substr(
                begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
            if (!piece.empty()) {
                phonemes.emplace_back(piece);
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1;
        }
        return phonemes;
    }

    srt::Expected<DictionaryTable> DictionaryTable::load(const fs::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return fileError(path, "S2P dictionary");
        }

        DictionaryTable table;
        std::string line;
        std::size_t number = 0;
        while (std::getline(file, line)) {
            ++number;
            stripBom(line, number == 1);
            if (line.empty()) {
                continue;
            }
            auto columns = twoColumns(path, number, line);
            if (!columns) {
                return columns.takeError();
            }
            auto phonemes = splitPronunciation(columns->second);
            if (phonemes.empty()) {
                return parseError(path, number, "no phonemes");
            }
            if (!table.m_entries.emplace(std::string(columns->first), std::move(phonemes)).second) {
                return parseError(path, number, "duplicate pronunciation");
            }
        }
        if (file.bad()) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to read the S2P dictionary: " + stdc::path::to_utf8(path));
        }
        return table;
    }

    std::vector<std::string> DictionaryTable::convert(std::string_view pronunciation) const {
        const auto it = m_entries.find(pronunciation);
        return it == m_entries.end() ? std::vector<std::string>() : it->second;
    }

    srt::Expected<MappingTable> MappingTable::load(const fs::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return fileError(path, "S2P mapping");
        }

        MappingTable table;
        std::string line;
        std::size_t number = 0;
        while (std::getline(file, line)) {
            ++number;
            stripBom(line, number == 1);
            if (line.empty()) {
                continue;
            }
            auto columns = twoColumns(path, number, line);
            if (!columns) {
                return columns.takeError();
            }
            if (!table.m_entries.emplace(std::string(columns->first), std::string(columns->second))
                     .second) {
                return parseError(path, number, "duplicate source phoneme");
            }
        }
        if (file.bad()) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to read the S2P mapping: " + stdc::path::to_utf8(path));
        }
        return table;
    }

    std::vector<std::string> MappingTable::convert(std::string_view pronunciation) const {
        auto phonemes = splitPronunciation(pronunciation);
        for (auto &phoneme : phonemes) {
            if (const auto it = m_entries.find(phoneme); it != m_entries.end()) {
                phoneme = it->second;
            }
        }
        return phonemes;
    }

    std::vector<std::string> MappingTable::targets() const {
        std::set<std::string> unique;
        for (const auto &[source, target] : m_entries) {
            unique.insert(target);
        }
        return {unique.begin(), unique.end()};
    }

}
