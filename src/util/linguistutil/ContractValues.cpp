#include "ContractValues.h"

#include <algorithm>
#include <utility>

#include "ManifestValues.h"

namespace wolf {

    srt::Expected<ContractExports> readContractExports(const srt::ContribSpec &spec,
                                                       const ExportsShape &shape,
                                                       std::string_view what) {
        const std::string context(what);
        ContractExports result;
        const auto &declared = spec.manifestExports();
        if (shape.optional && declared.isNull()) {
            return result;
        }
        if (!declared.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, context + " must be an object");
        }
        const auto &object = declared.toObject();

        for (const auto &[key, item] : object) {
            const bool known = (shape.languages && key == "languages") ||
                               (shape.strings != nullptr && key == shape.strings) ||
                               (shape.openSet && key == "openSet");
            if (!known) {
                return srt::Error(srt::Error::InvalidFormat,
                                  context + " has an unknown key: " + key);
            }
        }

        if (shape.languages) {
            if (const auto it = object.find("languages"); it != object.end()) {
                auto languages = readLanguageSchemes(it->second, context + " languages");
                if (!languages) {
                    return languages.takeError();
                }
                result.languages = languages.take();
            }
        }

        const auto strings = object.find(shape.strings);
        if (strings == object.end()) {
            if (shape.stringsRequired) {
                return srt::Error(srt::Error::InvalidFormat, context + " require " + shape.strings);
            }
        } else {
            auto read = readStringSet(strings->second, spec.declarationPath().parent_path(),
                                      context + " " + shape.strings);
            if (!read) {
                return read.takeError();
            }
            result.strings = read.take();
        }

        if (shape.openSet) {
            auto openSet = readFlag(object, "openSet", false, context);
            if (!openSet) {
                return openSet.takeError();
            }
            result.openSet = openSet.take();
        }
        return result;
    }

    srt::Expected<std::vector<LanguageMapEntry>>
        readLanguageMap(const srt::JsonValue &value, const LanguageMapRefCheck &checkRef) {
        if (!value.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, "languageMap must be an array");
        }
        std::vector<LanguageMapEntry> entries;
        const auto &array = value.toArray();
        entries.reserve(array.size());
        for (std::size_t i = 0; i < array.size(); ++i) {
            const auto position = "languageMap entry " + std::to_string(i);
            if (!array[i].isObject()) {
                return srt::Error(srt::Error::InvalidFormat, position + " must be an object");
            }
            LanguageMapEntry entry;
            for (const auto &[key, item] : array[i].toObject()) {
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      position + " " + key + " must be a non-empty string");
                }
                if (key == "language") {
                    entry.pair.language = item.toString();
                } else if (key == "scheme") {
                    entry.pair.scheme = item.toString();
                } else if (key == "ref") {
                    entry.ref = item.toString();
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      position + " has an unknown key: " + key);
                }
            }
            if (entry.pair.language.empty() || entry.pair.scheme.empty() || entry.ref.empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  position + " requires a language, a scheme and a ref");
            }
            if (checkRef) {
                if (auto checked = checkRef(position, entry.ref); !checked) {
                    return checked.takeError();
                }
            }
            for (const auto &earlier : entries) {
                if (earlier.pair == entry.pair) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      position + " repeats a pair already mapped");
                }
            }
            entries.push_back(std::move(entry));
        }
        if (entries.empty()) {
            return srt::Error(srt::Error::InvalidFormat, "languageMap must not be empty");
        }
        return entries;
    }

    srt::Expected<void> reconcileLanguageMap(const srt::ContribSpec &spec,
                                             const std::vector<LanguageMapEntry> &languageMap) {
        const auto &exports = spec.manifestExports();
        if (!exports.isObject() || exports.toObject().count("languages") == 0) {
            return srt::Error(srt::Error::InvalidFormat,
                              "this variant requires exports languages, which cannot be "
                              "derived from languageMap");
        }
        auto declared =
            readLanguageSchemes(exports.toObject().at("languages"), "exports languages");
        if (!declared) {
            return declared.takeError();
        }
        const auto pairs = declared.take();
        if (pairs.size() != languageMap.size()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "exports languages and languageMap describe different pairs");
        }
        for (const auto &entry : languageMap) {
            if (std::find(pairs.begin(), pairs.end(), entry.pair) == pairs.end()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "languageMap maps " + entry.pair.language + "/" +
                                      entry.pair.scheme + ", which exports languages omits");
            }
        }
        return {};
    }

    srt::Expected<int> readFormatVersion(const srt::JsonValue &value, int highest) {
        if (!value.isInt() || value.toInt() < 1) {
            return srt::Error(srt::Error::InvalidFormat,
                              "formatVersion must be a positive integer");
        }
        if (value.toInt() > highest) {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "this configuration declares format version " +
                                  std::to_string(value.toInt()) + ", above the highest version " +
                                  std::to_string(highest) +
                                  " that this build supports; upgrade wolf to load it");
        }
        return static_cast<int>(value.toInt());
    }

}
