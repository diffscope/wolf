#include "WolfPipeline_p.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageContrib.h>

namespace wolf {

    namespace Pipeline = Api::Language::L1;

    namespace {

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
                        if (target.interface() != Api::Language::L1::API_INTERFACE ||
                            target.variant() != Api::Language::L1::API_VARIANT ||
                            target.level() != Api::Language::L1::API_LEVEL) {
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

        class WolfPipelineExecInstance : public Pipeline::WolfPipelineExecInstance {
        public:
            WolfPipelineExecInstance(srt::SingerSpec &spec, std::vector<std::string> languageRoles)
                : Pipeline::WolfPipelineExecInstance(spec),
                  m_languageRoles(std::move(languageRoles)) {
            }

            const std::vector<std::string> &languageRoles() const override {
                return m_languageRoles;
            }

            srt::Expected<Api::Language::L1::LanguageExecInstance *> createLanguage(
                std::string_view role,
                const Api::Language::L1::LanguageRuntimeOptions &runtimeOptions) override {
                if (std::find(m_languageRoles.begin(), m_languageRoles.end(), role) ==
                    m_languageRoles.end()) {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "singer does not import the requested language role");
                }
                auto result = createChild(role, runtimeOptions);
                if (!result) {
                    return result.takeError();
                }
                return (*result)->as<Api::Language::L1::LanguageExecInstance>();
            }

        private:
            std::vector<std::string> m_languageRoles;
        };

        class WolfPipelineExtension : public Pipeline::WolfPipelineExtension {
        public:
            WolfPipelineExtension(srt::SingerSpec &spec, std::vector<std::string> languageRoles)
                : Pipeline::WolfPipelineExtension(
                      spec,
                      srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                                      Pipeline::WolfPipelineExecInstance>::ID),
                  m_languageRoles(std::move(languageRoles)) {
            }

            const std::vector<std::string> &languageRoles() const override {
                return m_languageRoles;
            }

            srt::Expected<std::unique_ptr<srt::SingerPipelineExecInstance>>
                createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != Pipeline::API_INTERFACE ||
                    runtimeOptions.variant() != Pipeline::API_VARIANT ||
                    runtimeOptions.level() != Pipeline::API_LEVEL) {
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

    }

    std::unique_ptr<srt::ContribImportValidator> createWolfImportValidator() {
        return std::unique_ptr<srt::ContribImportValidator>(new WolfImportValidator());
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        createWolfPipelineExtensions(srt::ContribSpec &spec) {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (!isSingerSpec(spec)) {
            return result;
        }
        std::vector<std::string> languageRoles;
        for (const auto &item : spec.imports()) {
            if (!isLanguageImport(item)) {
                continue;
            }
            languageRoles.push_back(item.role());
        }
        if (!languageRoles.empty()) {
            result.emplace_back(
                new WolfPipelineExtension(*spec.as<srt::SingerSpec>(), std::move(languageRoles)));
        }
        return result;
    }

}
