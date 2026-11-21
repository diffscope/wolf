#include "SingerLanguages.h"

#include <algorithm>
#include <set>
#include <utility>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerOpenVpiApiL1.h>
#include <synthrt/SVS/SingerContrib.h>

namespace wolf {

    namespace {

        srt::Expected<void> readLanguages(const srt::JsonObject &exports,
                                          const srt::ContribSpec &singer, SingerLanguages &result) {
            const auto languagesIt = exports.find("languages");
            if (languagesIt != exports.end()) {
                if (!languagesIt->second.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "singer languages must map language handles to import roles");
                }
                for (const auto &[handle, roleValue] : languagesIt->second.toObject()) {
                    if (handle.empty() || !roleValue.isString() || roleValue.toString().empty()) {
                        return srt::Error(
                            srt::Error::InvalidFormat,
                            "singer languages must map language handles to import roles");
                    }
                    const auto role = roleValue.toString();
                    if (!singer.findImport(role)) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "singer language " + handle +
                                              " refers to a nonexistent import role: " + role);
                    }
                    result.entries.push_back(SingerLanguage{handle, role});
                }
            }

            const auto defaultIt = exports.find("defaultLanguage");
            if (defaultIt != exports.end()) {
                if (!defaultIt->second.isString()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "singer defaultLanguage must be a string");
                }
                result.defaultLanguage = defaultIt->second.toString();
                const auto known = std::any_of(result.entries.begin(), result.entries.end(),
                                               [&](const SingerLanguage &entry) {
                                                   return entry.language == result.defaultLanguage;
                                               });
                if (!known) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "singer defaultLanguage is not a key of languages: " +
                                          result.defaultLanguage);
                }
            } else if (!result.entries.empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "a singer that declares languages must declare defaultLanguage");
            }
            return {};
        }

        srt::Expected<void> readReservedPhonemes(const srt::JsonObject &exports,
                                                 SingerLanguages &result) {
            const auto it = exports.find("reservedPhonemes");
            if (it == exports.end()) {
                return {};
            }
            if (!it->second.isArray()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "singer reservedPhonemes must be an array");
            }
            std::set<std::string> seen;
            for (const auto &item : it->second.toArray()) {
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(
                        srt::Error::InvalidFormat,
                        "singer reservedPhonemes must contain only non-empty strings");
                }
                auto token = item.toString();
                if (!seen.insert(token).second) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "singer reservedPhonemes contains " + token + " twice");
                }
                result.reservedPhonemes.push_back(std::move(token));
            }
            return {};
        }

    }

    srt::Expected<SingerLanguages> readSingerLanguages(const srt::ContribSpec &singer) {
        if (singer.locator().category() != srt::SingerCategory::NAME) {
            return srt::Error(srt::Error::InvalidArgument,
                              "the language map belongs to singer declarations, not to " +
                                  singer.locator().category());
        }

        SingerLanguages result;
        if (const auto *exports = singer.exports();
            exports && singer.triplet() == ds::Api::DiffSinger::L1::OpenVpi::Triplet) {
            // After Load the DiffSinger provider has interpreted the exports.
            const auto *typed = exports->as<ds::Api::DiffSinger::L1::Exports>();
            result.entries.reserve(typed->languages.size());
            for (const auto &[language, role] : typed->languages) {
                result.entries.push_back(SingerLanguage{language, role});
            }
            result.defaultLanguage = typed->defaultLanguage;
            result.reservedPhonemes = typed->reservedPhonemes;
            return result;
        }

        // DataOnly has no typed exports, so read the expanded manifest directly.
        const auto &manifest = singer.manifestExports();
        if (manifest.isNull()) {
            return result;
        }
        if (!manifest.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "singer exports must be an object");
        }
        const auto &exports = manifest.toObject();
        if (auto read = readLanguages(exports, singer, result); !read) {
            return read.takeError();
        }
        if (auto read = readReservedPhonemes(exports, result); !read) {
            return read.takeError();
        }
        return result;
    }

}