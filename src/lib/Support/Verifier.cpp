#include "Verifier.h"

#include "Files.h"
#include "ManifestValues.h"

#include <cstdint>
#include <fstream>
#include <set>
#include <utility>

#include <re2/re2.h>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace {

        constexpr char REGEX[] = "regex";
        constexpr char ARRAY[] = "array";
        constexpr char DICT[] = "dict";

        /// Memory budget of one compiled expression. RE2 rejects a program larger than its budget
        /// instead of growing without bound, so that a pathological pattern in a declaration fails
        /// the package at load instead of exhausting memory. The shipped patterns require a small
        /// fraction of this budget.
        constexpr std::int64_t REGEX_MEMORY_BYTES = 8 << 20;

        /// One compiled entry. A regex entry holds the expression, and the other two types hold a
        /// word set. Both forms are small, and one struct is clearer than a class hierarchy.
        struct Compiled {
            std::unique_ptr<RE2> regex;
            std::set<std::string> words;
            VerifyMode mode = VerifyMode::Convert;

            bool matches(const std::string &word) const {
                if (regex) {
                    return RE2::FullMatch(word, *regex);
                }
                return words.find(word) != words.end();
            }
        };

        /// Merges the expressions of one entry into a single alternation.
        ///
        /// Alternation has the lowest precedence, so the merged expression fully matches a word if
        /// any branch fully matches it, which is the semantics of a pattern list. Each branch is
        /// wrapped in its own non-capturing group, because an inline flag such as (?i) applies to
        /// the remainder of the enclosing group. Without the groups, a flag in one pattern would
        /// change the matching of every later pattern of the entry.
        std::string mergePatterns(const std::vector<std::string> &patterns) {
            std::string merged;
            for (const auto &pattern : patterns) {
                if (!merged.empty()) {
                    merged += '|';
                }
                merged += "(?:";
                merged += pattern;
                merged += ')';
            }
            return merged;
        }

        /// Reads the first tab separated column of every non-empty line.
        srt::Expected<void> loadWords(const fs::path &path, std::set<std::string> &words) {
            std::ifstream file;
            if (auto opened = openForReading(file, path, "verify dictionary"); !opened) {
                return opened.takeError();
            }
            std::string line;
            bool first = true;
            while (std::getline(file, line)) {
                stripLineDecorations(line, first);
                first = false;
                if (line.empty()) {
                    continue;
                }
                auto word = line.substr(0, line.find('\t'));
                if (!word.empty()) {
                    words.insert(std::move(word));
                }
            }
            return {};
        }

    }

    class Verifier::Impl {
    public:
        std::vector<Compiled> entries;
    };

    Verifier::Verifier() : _impl(std::make_unique<Impl>()) {
    }

    Verifier::Verifier(Verifier &&other) noexcept = default;

    Verifier &Verifier::operator=(Verifier &&other) noexcept = default;

    Verifier::~Verifier() = default;

    srt::Expected<Verifier> Verifier::create(const std::vector<VerifyEntry> &entries,
                                             const fs::path &base) {
        Verifier verifier;
        verifier._impl->entries.reserve(entries.size());
        for (const auto &entry : entries) {
            Compiled compiled;
            compiled.mode = entry.mode;
            if (entry.type == REGEX) {
                RE2::Options options;
                options.set_encoding(RE2::Options::EncodingUTF8);
                options.set_log_errors(false);
                options.set_max_mem(REGEX_MEMORY_BYTES);
                compiled.regex = std::make_unique<RE2>(mergePatterns(entry.value), options);
                if (!compiled.regex->ok()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "invalid verify expression: " + compiled.regex->error());
                }
            } else if (entry.type == ARRAY) {
                compiled.words.insert(entry.value.begin(), entry.value.end());
            } else if (entry.type == DICT) {
                for (const auto &item : entry.value) {
                    auto path = pathFromManifest(item);
                    if (path.is_relative()) {
                        path = base / path;
                    }
                    if (auto loaded = loadWords(path.lexically_normal(), compiled.words); !loaded) {
                        return loaded.takeError();
                    }
                }
            } else {
                return srt::Error(srt::Error::InvalidFormat,
                                  "unknown verify entry type: " + entry.type);
            }
            verifier._impl->entries.push_back(std::move(compiled));
        }
        return verifier;
    }

    std::vector<VerifyMode> Verifier::classify(const std::vector<std::string> &words) const {
        std::vector<VerifyMode> modes(words.size(), VerifyMode::Copy);
        for (const auto &entry : _impl->entries) {
            for (std::size_t i = 0; i < words.size(); ++i) {
                if (entry.matches(words[i])) {
                    modes[i] = entry.mode;
                }
            }
        }
        return modes;
    }

    srt::Expected<std::vector<VerifyEntry>> readVerifyEntries(const srt::JsonValue &value,
                                                              std::string_view what) {
        const std::string context(what);
        if (!value.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, context + " must be an array");
        }
        std::vector<VerifyEntry> entries;
        const auto &array = value.toArray();
        entries.reserve(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const auto position = context + " entry " + std::to_string(i);
            if (!array[i].isObject()) {
                return srt::Error(srt::Error::InvalidFormat, position + " must be an object");
            }
            VerifyEntry entry;
            bool hasMode = false;
            for (const auto &[key, item] : array[i].toObject()) {
                if (key == "type") {
                    if (!item.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " type must be a string");
                    }
                    entry.type = item.toString();
                } else if (key == "value") {
                    if (!item.isArray()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " value must be an array");
                    }
                    for (const auto &element : item.toArray()) {
                        if (!element.isString() || element.toString().empty()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              position + " value must hold non-empty strings");
                        }
                        entry.value.push_back(element.toString());
                    }
                } else if (key == "mode") {
                    if (!item.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " mode must be a string");
                    }
                    const auto &mode = item.toString();
                    if (mode == "convert") {
                        entry.mode = VerifyMode::Convert;
                    } else if (mode == "copy") {
                        entry.mode = VerifyMode::Copy;
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " has an unknown mode: " + mode);
                    }
                    hasMode = true;
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      position + " has an unknown key: " + key);
                }
            }
            if (entry.type.empty()) {
                return srt::Error(srt::Error::InvalidFormat, position + " requires a type");
            }
            // Checked here as well as at compilation, so that a misspelled type is reported at the
            // entry that declares it instead of as a rule that compiled to nothing.
            if (entry.type != REGEX && entry.type != ARRAY && entry.type != DICT) {
                return srt::Error(srt::Error::InvalidFormat,
                                  position + " has an unknown type: " + entry.type);
            }
            if (entry.value.empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  position + " requires a non-empty value");
            }
            if (!hasMode) {
                return srt::Error(srt::Error::InvalidFormat, position + " requires a mode");
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    }

}
