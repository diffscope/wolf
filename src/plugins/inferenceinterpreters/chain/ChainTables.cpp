#include "ChainTables.h"

#include <cctype>
#include <fstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/utf.h>

namespace fs = std::filesystem;

namespace wolf::chain {

    namespace {

        /// Strips a CMU style variant suffix so that word(2) joins the group of word.
        std::string baseWord(std::string word) {
            if (word.size() < 4 || word.back() != ')') {
                return word;
            }
            const auto open = word.rfind('(');
            if (open == std::string::npos || open == 0 || open + 2 > word.size() - 1) {
                return word;
            }
            for (auto i = open + 1; i + 1 < word.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(word[i])) == 0) {
                    return word;
                }
            }
            word.resize(open);
            return word;
        }

        char32_t lowercaseCodePoint(char32_t code) {
            if (code >= U'A' && code <= U'Z') {
                return code + 0x20;
            }
            if (code >= 0xC0 && code <= 0xDE && code != 0xD7) {
                return code + 0x20;
            }
            if (code >= 0x0100 && code <= 0x017E && (code % 2) == 0 && code != 0x0130) {
                return code + 1;
            }
            if (code >= 0x0410 && code <= 0x042F) {
                return code + 0x20;
            }
            if (code >= 0x0400 && code <= 0x040F) {
                return code + 0x50;
            }
            return code;
        }

    }

    namespace {

        /// One diagnosis about one line of a file, naming the file and the line. A dictionary runs
        /// to thousands of lines, so a message that cannot say which one is wrong is not usable.
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

    }

    srt::Expected<ChainDictionary> ChainDictionary::load(const fs::path &path) {
        std::ifstream file(path);
        if (!file) {
            return fileError(path, "dictionary");
        }
        ChainDictionary dictionary;
        std::string line;
        std::size_t number = 0;
        while (std::getline(file, line)) {
            ++number;
            if (number == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) {
                line.erase(0, 3);
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            const auto tab = line.find('\t');
            if (tab == std::string::npos) {
                return parseError(path, number, "missing tab separator");
            }
            if (line.find('\t', tab + 1) != std::string::npos) {
                return parseError(path, number, "multiple tab separators");
            }
            if (tab == 0) {
                return parseError(path, number, "empty first column");
            }
            if (tab + 1 == line.size()) {
                return parseError(path, number, "empty second column");
            }
            auto word = baseWord(line.substr(0, tab));
            dictionary.m_entries[std::move(word)].push_back(line.substr(tab + 1));
        }
        if (file.bad()) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to read the dictionary: " + stdc::path::to_utf8(path));
        }
        if (dictionary.m_entries.empty()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the dictionary holds no entries: " + stdc::path::to_utf8(path));
        }
        return dictionary;
    }

    const std::vector<std::string> *ChainDictionary::find(const std::string &word) const {
        const auto it = m_entries.find(word);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    std::size_t ChainDictionary::size() const noexcept {
        return m_entries.size();
    }

    std::string toLowercase(const std::string &text) {
        auto points = stdc::utf::utf8_to_utf32(text, stdc::utf::replace);
        for (auto &code : points) {
            code = lowercaseCodePoint(code);
        }
        return stdc::utf::utf32_to_utf8(points, stdc::utf::replace);
    }

    std::string stripTrailingSpace(const std::string &text) {
        const auto end = text.find_last_not_of(" \t\n\r");
        return end == std::string::npos ? std::string() : text.substr(0, end + 1);
    }

    std::string addSpaceBetweenPhones(const std::string &text) {
        std::string result;
        result.reserve(text.size());
        bool afterPhone = false;
        for (const char character : text) {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
                result += character;
                afterPhone = true;
                continue;
            }
            if (afterPhone && character != ' ') {
                result += ' ';
            }
            result += character;
            afterPhone = false;
        }
        return result;
    }

}
