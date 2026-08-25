#include "WolfLanguageInterpreter.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageContrib.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace Lang = Api::Language::L1;

    namespace {

        srt::Expected<srt::JsonValue> readJsonFile(const fs::path &path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return srt::Error(srt::Error::FileNotOpen, "failed to open language exports file");
            }
            std::ostringstream stream;
            stream << file.rdbuf();
            stdc::json::ParseError error;
            auto value = srt::JsonValue::fromJson(stream.str(), true, &error);
            if (error) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language exports file contains invalid JSON");
            }
            return value;
        }

        srt::Expected<void> readPhonemes(std::vector<std::string> &phonemes,
                                         const srt::JsonValue &value, const fs::path &basePath) {
            const srt::JsonValue *source = &value;
            srt::JsonValue fileValue;
            if (value.isString()) {
                auto path = stdc::path::from_utf8(value.toString());
                if (path.is_relative()) {
                    path = basePath / path;
                }
                auto result = readJsonFile(path.lexically_normal());
                if (!result) {
                    return result.takeError();
                }
                fileValue = result.take();
                source = &fileValue;
            }
            if (!source->isArray()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language exports phonemes must be an array or JSON path");
            }
            std::set<std::string> unique;
            for (const auto &item : source->toArray()) {
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "language phoneme entries must be nonempty strings");
                }
                if (!unique.insert(item.toString()).second) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "language phoneme entries must be unique");
                }
                phonemes.push_back(item.toString());
            }
            return {};
        }

        srt::Expected<void> validateKnownRole(const srt::ContribImport &item,
                                              std::string_view expectedInterface) {
            if (!item.binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language import does not have a runtime binding");
            }
            if (item.binding()->target().interface() != expectedInterface) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language import role targets an incompatible interface");
            }
            return {};
        }

    }

    WolfLanguageInterpreter::WolfLanguageInterpreter() = default;

    WolfLanguageInterpreter::~WolfLanguageInterpreter() = default;

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        WolfLanguageInterpreter::createExports(const srt::ContribSpec &spec) const {
        if (!spec.manifestExports().isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "language exports must be an object");
        }
        const auto &object = spec.manifestExports().toObject();
        const auto it = object.find("phonemes");
        if (it == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "language exports require phonemes");
        }
        auto result = std::make_unique<Lang::LanguageExports>();
        const auto basePath = spec.as<LanguageSpec>()->declarationPath().parent_path();
        if (auto parsed = readPhonemes(result->phonemes, it->second, basePath); !parsed) {
            return parsed.takeError();
        }
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        WolfLanguageInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        if (!spec.manifestConfiguration().isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "language configuration must be an object");
        }
        if (!spec.manifestConfiguration().toObject().empty()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the wolf language configuration must be empty at Level 1");
        }
        return std::unique_ptr<srt::ContribConfiguration>(new Lang::LanguageConfiguration());
    }

    srt::Expected<void>
        WolfLanguageInterpreter::validateImports(const srt::ContribSpec &spec) const {
        bool hasG2P = false;
        bool hasS2P = false;
        for (const auto &item : spec.imports()) {
            if (item.role() == "g2p") {
                if (auto result = validateKnownRole(item, Api::G2P::L1::API_INTERFACE); !result) {
                    return result.takeError();
                }
                hasG2P = true;
            } else if (item.role() == "s2p") {
                if (auto result = validateKnownRole(item, Api::S2P::L1::API_INTERFACE); !result) {
                    return result.takeError();
                }
                hasS2P = true;
            } else if (item.role() == "onset") {
                if (auto result = validateKnownRole(item, Api::Onset::L1::API_INTERFACE); !result) {
                    return result.takeError();
                }
            }
        }
        if (!hasG2P || !hasS2P) {
            return srt::Error(srt::Error::InvalidFormat,
                              "language imports require g2p and s2p roles");
        }
        return {};
    }

}
