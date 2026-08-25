#include "WolfLanguageProvider.h"

#include "WolfPipelineExecInstance.h"

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
#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageContrib.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace Lang = Api::Language::L1;

    namespace {

        class LanguageExecFactory : public srt::ContribExecFactory {
        public:
            explicit LanguageExecFactory(srt::ContribImportBinding &binding) : m_binding(&binding) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExecInstance>>
                create(const srt::ContribRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != Lang::API_INTERFACE ||
                    runtimeOptions.variant() != Lang::API_VARIANT ||
                    runtimeOptions.level() != Lang::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "language runtime options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::ContribExecInstance>(
                    new Lang::LanguageExecInstance(*m_binding->target().as<wolf::LanguageSpec>()));
            }

        private:
            srt::ContribImportBinding *m_binding;
        };

        bool isSingerSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == "singer";
        }

        bool isLanguageSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == LANGUAGE_CATEGORY;
        }

        bool isLanguageImport(const srt::ContribImport &item) {
            return item.binding() &&
                   item.binding()->target().locator().category() == LANGUAGE_CATEGORY;
        }

        srt::Expected<void> validateLanguageRole(const srt::ContribImport &item,
                                                 std::string_view expectedInterface) {
            if (!item.binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language import has no prepared binding");
            }
            if (item.binding()->target().interface() != expectedInterface) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "language import role targets an incompatible interface");
            }
            if (!item.execFactory()) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "language import has no execution factory");
            }
            return {};
        }

        class WolfImportValidator : public srt::ContribImportValidator {
        public:
            srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
                if (isLanguageSpec(spec)) {
                    bool hasG2P = false;
                    bool hasS2P = false;
                    for (const auto &item : spec.imports()) {
                        if (item.role() == "g2p") {
                            if (auto result =
                                    validateLanguageRole(item, Api::G2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            hasG2P = true;
                        } else if (item.role() == "s2p") {
                            if (auto result =
                                    validateLanguageRole(item, Api::S2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            hasS2P = true;
                        } else if (item.role() == "onset") {
                            if (auto result =
                                    validateLanguageRole(item, Api::Onset::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                        }
                    }
                    if (!hasG2P || !hasS2P) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "language imports require g2p and s2p roles");
                    }
                }
                if (isSingerSpec(spec)) {
                    for (const auto &item : spec.imports()) {
                        if (!isLanguageImport(item)) {
                            continue;
                        }
                        const auto &target = item.binding()->target();
                        if (target.interface() != Lang::API_INTERFACE ||
                            target.variant() != Lang::API_VARIANT ||
                            target.level() != Lang::API_LEVEL) {
                            return srt::Error(
                                srt::Error::InvalidFormat,
                                "singer language import has an incompatible contract identity");
                        }
                        if (!item.execFactory()) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              "language import has no execution factory");
                        }
                    }
                }
                return {};
            }
        };

        class WolfPipelineExtension : public Lang::WolfPipelineExtension {
        public:
            WolfPipelineExtension(srt::SingerSpec &spec, std::vector<std::string> languageRoles)
                : Lang::WolfPipelineExtension(
                      spec, srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                                            Lang::WolfPipelineExecInstance>::ID),
                  m_languageRoles(std::move(languageRoles)) {
            }

            const std::vector<std::string> &languageRoles() const override {
                return m_languageRoles;
            }

            srt::Expected<std::unique_ptr<srt::SingerPipelineExecInstance>>
                createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != Lang::API_INTERFACE ||
                    runtimeOptions.variant() != Lang::API_VARIANT ||
                    runtimeOptions.level() != Lang::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "wolf pipeline options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::SingerPipelineExecInstance>(
                    new WolfPipelineExecInstance(spec(), m_languageRoles));
            }

        private:
            std::vector<std::string> m_languageRoles;
        };

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

    }

    WolfLanguageProvider::WolfLanguageProvider() = default;

    WolfLanguageProvider::~WolfLanguageProvider() = default;

    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        WolfLanguageProvider::createImportValidators() const {
        std::vector<std::unique_ptr<srt::ContribImportValidator>> result;
        result.emplace_back(new WolfImportValidator());
        return result;
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        WolfLanguageProvider::createExtensions(srt::ContribSpec &spec) const {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (!isSingerSpec(spec)) {
            return result;
        }
        std::vector<std::string> languageRoles;
        for (const auto &item : spec.imports()) {
            if (isLanguageImport(item)) {
                languageRoles.push_back(item.role());
            }
        }
        if (!languageRoles.empty()) {
            result.emplace_back(
                new WolfPipelineExtension(*spec.as<srt::SingerSpec>(), std::move(languageRoles)));
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        WolfLanguageProvider::createImportOptions(const srt::ContribSpec &target,
                                                  const srt::JsonValue &manifestOptions) const {
        if (!manifestOptions.isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "language import options must be an object");
        }
        if (target.interface() != Lang::API_INTERFACE || target.variant() != Lang::API_VARIANT ||
            target.level() != Lang::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument,
                              "language import target has an unsupported contract");
        }
        return std::unique_ptr<srt::ContribImportOptions>(new Lang::LanguageImportOptions());
    }

    srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
        WolfLanguageProvider::createExecFactory(srt::ContribImportBinding &binding) const {
        return std::unique_ptr<srt::ContribExecFactory>(new LanguageExecFactory(binding));
    }

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        WolfLanguageProvider::createExports(const srt::ContribSpec &spec) const {
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
        WolfLanguageProvider::createConfiguration(const srt::ContribSpec &spec) const {
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

}
