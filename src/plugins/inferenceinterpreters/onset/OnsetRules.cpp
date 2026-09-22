#include "OnsetRules.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Support/JSON.h>

namespace fs = std::filesystem;

namespace wolf::onset {

    namespace {

        constexpr char WILDCARD[] = "*";

        srt::Error invalid(const fs::path &path, const std::string &what) {
            return srt::Error(srt::Error::InvalidFormat, stdc::path::to_utf8(path) + ": " + what);
        }

        /// Tells a rule file that is not there from one that is there and cannot be read.
        ///
        /// An open call that fails says only that it failed, and the two faults call for different
        /// answers: a missing rule file is a voicebank that was installed wrong, an unreadable one
        /// is a host problem. The code is what a caller switches on, so it is decided by looking at
        /// the path rather than assumed from the failure.
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

    srt::Expected<RuleTable> RuleTable::load(const fs::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return fileError(path, "onset rules");
        }
        std::ostringstream stream;
        stream << file.rdbuf();
        if (file.bad()) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to read the onset rules: " + stdc::path::to_utf8(path));
        }

        stdc::json::ParseError error;
        const auto document = srt::JsonValue::fromJson(stream.str(), true, &error);
        if (error || !document.isObject()) {
            // The parser knows where it stopped and a rule file is written by hand, so the position
            // it reports is the difference between a usable diagnosis and a guess.
            return invalid(path, error ? error.message()
                                       : "the file does not contain a JSON object");
        }
        const auto &object = document.toObject();

        RuleTable table;

        // The version is checked before anything else, so a file written for a later shape is
        // refused by its declaration rather than by whichever key it happens to use.
        for (const auto &[key, item] : object) {
            if (key == "formatVersion") {
                if (!item.isInt() || item.toInt() < 1) {
                    return invalid(path, "formatVersion must be a positive integer");
                }
                if (item.toInt() > RuleTable::FORMAT_VERSION) {
                    return srt::Error(srt::Error::FeatureNotSupported,
                                      stdc::path::to_utf8(path) +
                                          ": these onset rules declare format version " +
                                          std::to_string(item.toInt()) + ", above the " +
                                          std::to_string(RuleTable::FORMAT_VERSION) +
                                          " this build reads; upgrade wolf to load it");
                }
            } else if (key != "phonemeTypes" && key != "rules") {
                return invalid(path, "unknown key in the rule file: " + key);
            }
        }

        const auto typesIt = object.find("phonemeTypes");
        if (typesIt == object.end() || !typesIt->second.isObject() ||
            typesIt->second.toObject().empty()) {
            return invalid(path, "phonemeTypes must be a non-empty object");
        }
        for (const auto &[phoneme, type] : typesIt->second.toObject()) {
            if (!type.isString() || type.toString().empty()) {
                return invalid(path, "the type of " + phoneme + " must be a non-empty string");
            }
            if (type.toString() == WILDCARD) {
                return invalid(path, "a phoneme type may not be named " + std::string(WILDCARD));
            }
            table.m_types.emplace(phoneme, type.toString());
        }

        const auto rulesIt = object.find("rules");
        if (rulesIt == object.end() || !rulesIt->second.isArray()) {
            return invalid(path, "rules must be an array");
        }
        for (const auto &item : rulesIt->second.toArray()) {
            if (!item.isObject()) {
                return invalid(path, "every rule must be an object");
            }
            const auto &rule = item.toObject();

            for (const auto &entry : rule) {
                if (entry.first != "pattern" && entry.first != "onsets") {
                    return invalid(path, "unknown key in a rule: " + entry.first);
                }
            }

            const auto patternIt = rule.find("pattern");
            if (patternIt == rule.end() || !patternIt->second.isArray() ||
                patternIt->second.toArray().empty()) {
                return invalid(path, "every rule needs a non-empty pattern");
            }
            Rule parsed;
            for (const auto &segment : patternIt->second.toArray()) {
                if (!segment.isString() || segment.toString().empty()) {
                    return invalid(path, "pattern segments must be non-empty strings");
                }
                parsed.pattern.push_back(segment.toString());
            }

            const auto onsetsIt = rule.find("onsets");
            if (onsetsIt == rule.end() || !onsetsIt->second.isArray()) {
                return invalid(path, "every rule needs an onsets array");
            }
            for (const auto &index : onsetsIt->second.toArray()) {
                if (!index.isInt() || index.toInt() < 0 ||
                    static_cast<std::size_t>(index.toInt()) >= parsed.pattern.size()) {
                    return invalid(path, "an onset index falls outside its pattern");
                }
                parsed.onsets.push_back(static_cast<std::size_t>(index.toInt()));
            }
            table.m_rules.push_back(std::move(parsed));
        }
        return table;
    }

    std::vector<bool> RuleTable::mark(const std::vector<std::string> &phonemes) const {
        std::vector<bool> onsets(phonemes.size(), false);

        std::size_t position = 0;
        while (position < phonemes.size()) {
            std::optional<Fit> best;
            const Rule *chosen = nullptr;

            for (const auto &rule : m_rules) {
                if (position + rule.pattern.size() > phonemes.size()) {
                    continue;
                }
                Fit fit;
                fit.length = rule.pattern.size();
                fit.firstExact = fit.length;
                bool fits = true;
                for (std::size_t i = 0; i < rule.pattern.size(); ++i) {
                    const auto &segment = rule.pattern[i];
                    const auto &phoneme = phonemes[position + i];
                    if (segment == phoneme) {
                        // A literal beats the type it belongs to, so a rule written for one
                        // phoneme overrides the rule written for its class.
                        ++fit.exact;
                        fit.firstExact = std::min(fit.firstExact, i);
                        continue;
                    }
                    if (segment == WILDCARD) {
                        continue;
                    }
                    const auto type = m_types.find(phoneme);
                    if (type != m_types.end() && type->second == segment) {
                        ++fit.typed;
                        continue;
                    }
                    // A phoneme with no registered type matches only a literal or the wildcard.
                    fits = false;
                    break;
                }
                if (!fits) {
                    continue;
                }
                const auto better =
                    !best || fit.length > best->length ||
                    (fit.length == best->length && fit.exact > best->exact) ||
                    (fit.length == best->length && fit.exact == best->exact &&
                     fit.typed > best->typed) ||
                    (fit.length == best->length && fit.exact == best->exact &&
                     fit.typed == best->typed && fit.firstExact < best->firstExact);
                if (better) {
                    best = fit;
                    chosen = &rule;
                }
            }

            if (!chosen) {
                ++position;
                continue;
            }
            for (const auto offset : chosen->onsets) {
                onsets[position + offset] = true;
            }
            position += chosen->pattern.size();
        }
        return onsets;
    }

}
