#include "ManifestValues.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace {

        srt::Expected<srt::JsonValue> readJsonFile(const fs::path &path, std::string_view what) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return srt::Error(srt::Error::FileNotOpen,
                                  "failed to open the file named by " + std::string(what));
            }
            std::ostringstream stream;
            stream << file.rdbuf();
            stdc::json::ParseError error;
            auto value = srt::JsonValue::fromJson(stream.str(), true, &error);
            if (error) {
                return srt::Error(srt::Error::InvalidFormat, "the file named by " +
                                                                 std::string(what) +
                                                                 " does not contain valid JSON");
            }
            return value;
        }

    }

    srt::Expected<std::vector<std::string>>
        readStringSet(const srt::JsonValue &value, const fs::path &base, std::string_view what) {
        const srt::JsonValue *source = &value;
        srt::JsonValue fromFile;
        if (value.isString()) {
            auto path = pathFromManifest(value.toString());
            if (path.is_relative()) {
                path = base / path;
            }
            auto result = readJsonFile(path.lexically_normal(), what);
            if (!result) {
                return result.takeError();
            }
            fromFile = result.take();
            source = &fromFile;
        }
        if (!source->isArray()) {
            return srt::Error(srt::Error::InvalidFormat,
                              std::string(what) + " must be an array or a path to one");
        }

        std::vector<std::string> result;
        std::set<std::string> seen;
        for (const auto &item : source->toArray()) {
            if (!item.isString() || item.toString().empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " entries must be nonempty strings");
            }
            if (!seen.insert(item.toString()).second) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " entries must be unique");
            }
            result.push_back(item.toString());
        }
        return result;
    }

    fs::path pathFromManifest(std::string_view text) {
        // `\` separates on Windows and is an ordinary character in a POSIX file name, so a reader
        // that handed the declared string straight to the file system would open two different
        // files on the two platforms. Rewrite it before the host sees anything. synthrt's loader
        // normalizes the same way for the paths it resolves itself (pathFromManifest in
        // PackageLoader).
        std::string normalized(text);
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return stdc::path::from_utf8(normalized);
    }

    /// Matches an ISO 639-3 code: exactly three ASCII lowercase letters.
    bool isLanguageHandle(std::string_view value) {
        return value.size() == 3 && std::all_of(value.begin(), value.end(),
                                                [](char ch) { return ch >= 'a' && ch <= 'z'; });
    }

    /// Matches [a-z0-9]+( "-" [a-z0-9]+ )*
    bool isSchemeName(std::string_view value) {
        if (value.empty() || value.front() == '-' || value.back() == '-') {
            return false;
        }
        bool afterHyphen = false;
        for (const auto ch : value) {
            if (ch == '-') {
                if (afterHyphen) {
                    return false;
                }
                afterHyphen = true;
                continue;
            }
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
                return false;
            }
            afterHyphen = false;
        }
        return true;
    }

    srt::Expected<bool> readFlag(const srt::JsonObject &object, std::string_view key, bool fallback,
                                 std::string_view what) {
        const auto it = object.find(std::string(key));
        if (it == object.end()) {
            return fallback;
        }
        if (!it->second.isBool()) {
            return srt::Error(srt::Error::InvalidFormat,
                              std::string(what) + " " + std::string(key) + " must be a boolean");
        }
        return it->second.toBool();
    }

    srt::Expected<void> requireNoImportOptions(const srt::JsonValue &value, std::string_view what) {
        if (value.isNull()) {
            return {};
        }
        if (!value.isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              std::string(what) + " import options must be an object");
        }
        return rejectUnknownKeys(value.toObject(), {}, std::string(what) + " import options");
    }

    srt::Expected<void> rejectUnknownKeys(const srt::JsonObject &object,
                                          std::initializer_list<const char *> allowed,
                                          std::string_view what) {
        for (const auto &[key, item] : object) {
            bool known = false;
            for (auto candidate : allowed) {
                if (key == candidate) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " has an unknown key: " + key);
            }
        }
        return {};
    }

    srt::Expected<std::vector<Api::Common::L1::LanguageScheme>>
        readLanguageSchemes(const srt::JsonValue &value, std::string_view what) {
        if (!value.isArray()) {
            return srt::Error(srt::Error::InvalidFormat, std::string(what) + " must be an array");
        }

        std::vector<Api::Common::L1::LanguageScheme> result;
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto &item : value.toArray()) {
            if (!item.isObject()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " entries must be objects");
            }
            const auto &object = item.toObject();
            if (auto known =
                    rejectUnknownKeys(object, {"language", "scheme"}, std::string(what) + " entry");
                !known) {
                return known.takeError();
            }
            const auto languageIt = object.find("language");
            const auto schemeIt = object.find("scheme");
            if (languageIt == object.end() || !languageIt->second.isString() ||
                schemeIt == object.end() || !schemeIt->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) +
                                      " entries must carry string language and scheme");
            }
            // The same grammar the identity fields use. A pair outside it can never match a
            // language, so declaring one is a dead statement worth naming at load.
            if (!isLanguageHandle(languageIt->second.toString())) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) +
                                      " has a language that is not three lower "
                                      "case letters: " +
                                      languageIt->second.toString());
            }
            if (!isSchemeName(schemeIt->second.toString())) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) +
                                      " has a malformed scheme: " + schemeIt->second.toString());
            }
            auto pair = std::make_pair(languageIt->second.toString(), schemeIt->second.toString());
            if (!seen.insert(pair).second) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " entries must be unique");
            }
            result.push_back(
                Api::Common::L1::LanguageScheme(std::move(pair.first), std::move(pair.second)));
        }
        return result;
    }

}
