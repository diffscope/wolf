#include "WolfLinguistProvider.h"

#include "WolfPipelineExecutive.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace LinguistApi = Api::Linguist::L1;

    namespace {

        class LinguistExecutiveFactory : public srt::ContribExecutiveFactory {
        public:
            explicit LinguistExecutiveFactory(srt::ContribImportBinding &binding)
                : m_binding(&binding) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExecutive>>
                create(const srt::ContribRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != LinguistApi::API_INTERFACE ||
                    runtimeOptions.variant() != LinguistApi::API_VARIANT ||
                    runtimeOptions.level() != LinguistApi::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "linguist runtime options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::ContribExecutive>(
                    new LinguistApi::LinguistExecutive(
                        *m_binding->target().as<wolf::LinguistSpec>()));
            }

        private:
            srt::ContribImportBinding *m_binding;
        };

        bool isSingerSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == "singer";
        }

        bool isLinguistSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == LINGUIST_CATEGORY;
        }

        bool isLinguistTarget(const srt::ContribImport &item) {
            return item.binding() &&
                   item.binding()->target().locator().category() == LINGUIST_CATEGORY;
        }

        bool hasLinguistRole(const srt::ContribImport &item) {
            constexpr std::string_view prefix = "linguist/";
            return item.role().size() > prefix.size() &&
                   item.role().compare(0, prefix.size(), prefix) == 0;
        }

        srt::Expected<void> validateLinguistRole(const srt::ContribImport &item,
                                                 std::string_view expectedInterface) {
            if (!item.binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist import has no prepared binding");
            }
            if (item.binding()->target().interface() != expectedInterface) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist import role targets an incompatible interface");
            }
            if (!item.executiveFactory()) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "linguist import has no execution factory");
            }
            return {};
        }

        class WolfImportValidator : public srt::ContribImportValidator {
        public:
            srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
                if (isLinguistSpec(spec)) {
                    bool hasG2P = false;
                    bool hasS2P = false;
                    for (const auto &item : spec.imports()) {
                        if (item.role() == "linguist/g2p") {
                            if (auto result =
                                    validateLinguistRole(item, Api::G2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            hasG2P = true;
                        } else if (item.role() == "linguist/s2p") {
                            if (auto result =
                                    validateLinguistRole(item, Api::S2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            hasS2P = true;
                        } else if (item.role() == "linguist/onset") {
                            if (auto result =
                                    validateLinguistRole(item, Api::Onset::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                        }
                    }
                    if (!hasG2P || !hasS2P) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "linguist imports require linguist/g2p and "
                                          "linguist/s2p roles");
                    }
                }
                if (isSingerSpec(spec)) {
                    for (const auto &item : spec.imports()) {
                        if (!isLinguistTarget(item)) {
                            continue;
                        }
                        if (!hasLinguistRole(item)) {
                            return srt::Error(
                                srt::Error::InvalidFormat,
                                "singer linguist imports require a linguist/* role");
                        }
                        const auto &target = item.binding()->target();
                        if (target.interface() != LinguistApi::API_INTERFACE ||
                            target.variant() != LinguistApi::API_VARIANT ||
                            target.level() != LinguistApi::API_LEVEL) {
                            return srt::Error(
                                srt::Error::InvalidFormat,
                                "singer linguist import has an incompatible contract identity");
                        }
                        if (!item.executiveFactory()) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              "linguist import has no execution factory");
                        }
                    }
                }
                return {};
            }
        };

        class WolfPipelineExtension : public LinguistApi::WolfPipelineExtension {
        public:
            WolfPipelineExtension(srt::SingerSpec &spec, std::vector<std::string> linguistRoles)
                : LinguistApi::WolfPipelineExtension(
                      spec, srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                                            LinguistApi::WolfPipelineExecutive>::ID),
                  m_linguistRoles(std::move(linguistRoles)) {
            }

            const std::vector<std::string> &linguistRoles() const override {
                return m_linguistRoles;
            }

            srt::Expected<std::unique_ptr<srt::SingerPipelineExecutive>>
                createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != LinguistApi::API_INTERFACE ||
                    runtimeOptions.variant() != LinguistApi::API_VARIANT ||
                    runtimeOptions.level() != LinguistApi::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "wolf pipeline options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::SingerPipelineExecutive>(
                    new WolfPipelineExecutive(spec(), m_linguistRoles));
            }

        private:
            std::vector<std::string> m_linguistRoles;
        };

        srt::Expected<srt::JsonValue> readJsonFile(const fs::path &path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return srt::Error(srt::Error::FileNotOpen, "failed to open linguist exports file");
            }
            std::ostringstream stream;
            stream << file.rdbuf();
            stdc::json::ParseError error;
            auto value = srt::JsonValue::fromJson(stream.str(), true, &error);
            if (error) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist exports file contains invalid JSON");
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
                                  "linguist exports phonemes must be an array or JSON path");
            }
            std::set<std::string> unique;
            for (const auto &item : source->toArray()) {
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "linguist phoneme entries must be nonempty strings");
                }
                if (!unique.insert(item.toString()).second) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "linguist phoneme entries must be unique");
                }
                phonemes.push_back(item.toString());
            }
            return {};
        }

    }

    WolfLinguistProvider::WolfLinguistProvider() = default;

    WolfLinguistProvider::~WolfLinguistProvider() = default;

    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        WolfLinguistProvider::createImportValidators() const {
        std::vector<std::unique_ptr<srt::ContribImportValidator>> result;
        result.emplace_back(new WolfImportValidator());
        return result;
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        WolfLinguistProvider::createExtensions(srt::ContribSpec &spec) const {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (!isSingerSpec(spec)) {
            return result;
        }
        std::vector<std::string> linguistRoles;
        for (const auto &item : spec.imports()) {
            if (isLinguistTarget(item) && hasLinguistRole(item)) {
                linguistRoles.push_back(item.role());
            }
        }
        if (!linguistRoles.empty()) {
            result.emplace_back(
                new WolfPipelineExtension(*spec.as<srt::SingerSpec>(), std::move(linguistRoles)));
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        WolfLinguistProvider::createImportOptions(const srt::ContribSpec &target,
                                                  const srt::JsonValue &manifestOptions) const {
        if (!manifestOptions.isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist import options must be an object");
        }
        if (target.interface() != LinguistApi::API_INTERFACE ||
            target.variant() != LinguistApi::API_VARIANT ||
            target.level() != LinguistApi::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument,
                              "linguist import target has an unsupported contract");
        }
        return std::unique_ptr<srt::ContribImportOptions>(
            new LinguistApi::LinguistImportOptions());
    }

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        WolfLinguistProvider::createExecutiveFactory(srt::ContribImportBinding &binding) const {
        return std::unique_ptr<srt::ContribExecutiveFactory>(new LinguistExecutiveFactory(binding));
    }

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        WolfLinguistProvider::createExports(const srt::ContribSpec &spec) const {
        if (!spec.manifestExports().isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "linguist exports must be an object");
        }
        const auto &object = spec.manifestExports().toObject();
        const auto it = object.find("phonemes");
        if (it == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "linguist exports require phonemes");
        }
        auto result = std::make_unique<LinguistApi::LinguistExports>();
        const auto basePath = spec.as<LinguistSpec>()->declarationPath().parent_path();
        if (auto parsed = readPhonemes(result->phonemes, it->second, basePath); !parsed) {
            return parsed.takeError();
        }
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        WolfLinguistProvider::createConfiguration(const srt::ContribSpec &spec) const {
        if (!spec.manifestConfiguration().isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist configuration must be an object");
        }
        if (!spec.manifestConfiguration().toObject().empty()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the wolf linguist configuration must be empty at Level 1");
        }
        return std::unique_ptr<srt::ContribConfiguration>(
            new LinguistApi::LinguistConfiguration());
    }

}
