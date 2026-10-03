#include "OnsetRules.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Support/JSON.h>

#include <wolf/Support/Files.h>

namespace fs = std::filesystem;

namespace wolf::onset {

    namespace {

        constexpr char WILDCARD[] = "*";

        srt::Error invalid(const fs::path &path, const std::string &what) {
            return srt::Error(srt::Error::InvalidFormat, stdc::path::to_utf8(path) + ": " + what);
        }

    }

    srt::Expected<RuleTable> RuleTable::load(const fs::path &path) {
        std::ifstream file;
        if (auto opened = openForReading(file, path, "onset rules"); !opened) {
            return opened.takeError();
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
            // The parser error records the position at which parsing stopped. Rule files are
            // written by hand, so that position is required for a usable diagnostic.
            return invalid(path, error ? error.message()
                                       : "the file does not contain a JSON object");
        }
        const auto &object = document.toObject();

        RuleTable table;

        // The version is read before any other key, so that a file written for a later format is
        // rejected because of its declared version rather than because of an unknown key. The
        // version is looked up directly instead of during the iteration below, because the object
        // iterates its keys in its own order and a later format may contain a key that sorts
        // before formatVersion.
        if (const auto versionIt = object.find("formatVersion"); versionIt != object.end()) {
            const auto &item = versionIt->second;
            if (!item.isInt() || item.toInt() < 1) {
                return invalid(path, "formatVersion must be a positive integer");
            }
            if (item.toInt() > RuleTable::FORMAT_VERSION) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  stdc::path::to_utf8(path) +
                                      ": the onset rules specify format version " +
                                      std::to_string(item.toInt()) +
                                      ", but this build supports format versions up to " +
                                      std::to_string(RuleTable::FORMAT_VERSION) +
                                      ". Upgrade wolf to load them.");
            }
        }

        for (const auto &[key, item] : object) {
            if (key != "formatVersion" && key != "phonemeTypes" && key != "rules") {
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
                return invalid(path, "every rule must have a non-empty pattern");
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
                return invalid(path, "every rule must have an onsets array");
            }
            for (const auto &index : onsetsIt->second.toArray()) {
                if (!index.isInt() || index.toInt() < 0 ||
                    static_cast<std::size_t>(index.toInt()) >= parsed.pattern.size()) {
                    return invalid(path, "an onset index is outside its pattern");
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
                        // A literal match ranks above a typed match, so a rule for a specific
                        // phoneme overrides the rule for its phoneme type.
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
