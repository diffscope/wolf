#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <synthrt/SVS/SingerContrib.h>

#include <wolf/Language/LanguageContrib.h>

namespace wolf::Api {

    Language::L1::LanguageExecInstance::LanguageExecInstance(srt::ContribSpec &spec)
        : ContribExecInstance(spec) {
    }

    Language::L1::LanguageExecInstance::~LanguageExecInstance() = default;

    srt::Expected<void> Language::L1::LanguageExecInstance::quit() {
        return {};
    }

    srt::Expected<void> Language::L1::LanguageExecInstance::wait() {
        return {};
    }

    Language::L1::WolfPipelineExecInstance::~WolfPipelineExecInstance() = default;

    Language::L1::WolfPipelineExtension::~WolfPipelineExtension() = default;

}

namespace wolf {

    namespace Pipeline = Api::Language::L1;

    namespace {

        class LanguageExecFactory : public srt::ContribExecFactory {
        public:
            explicit LanguageExecFactory(srt::ContribImportBinding &binding) : m_binding(&binding) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExecInstance>>
                create(const srt::ContribRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != Api::Language::L1::API_INTERFACE ||
                    runtimeOptions.variant() != Api::Language::L1::API_VARIANT ||
                    runtimeOptions.level() != Api::Language::L1::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "language runtime options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::ContribExecInstance>(
                    new Api::Language::L1::LanguageExecInstance(m_binding->target()));
            }

        private:
            srt::ContribImportBinding *m_binding;
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

        class WolfPipelineExtensionFactory : public srt::ContribSpecExtensionFactory {
        public:
            bool matches(const srt::ContribSpec &spec) const noexcept override {
                if (spec.locator().category() != "singer") {
                    return false;
                }
                for (const auto &item : spec.imports()) {
                    if (item.binding() &&
                        item.binding()->target().locator().category() == LANGUAGE_CATEGORY) {
                        return true;
                    }
                }
                return false;
            }

            srt::Expected<std::unique_ptr<srt::ContribSpecExtension>>
                create(srt::ContribSpec &spec) const override {
                std::vector<std::string> languageRoles;
                for (const auto &item : spec.imports()) {
                    if (!item.binding() ||
                        item.binding()->target().locator().category() != LANGUAGE_CATEGORY) {
                        continue;
                    }
                    if (!item.execFactory()) {
                        return srt::Error(srt::Error::FeatureNotSupported,
                                          "language import has no execution factory");
                    }
                    languageRoles.push_back(item.role());
                }
                return std::unique_ptr<srt::ContribSpecExtension>(new WolfPipelineExtension(
                    *spec.as<srt::SingerSpec>(), std::move(languageRoles)));
            }
        };

    }

    srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
        createLanguageExecFactory(srt::ContribImportBinding &binding) {
        return std::unique_ptr<srt::ContribExecFactory>(new LanguageExecFactory(binding));
    }

}

static srt::ContribSpecExtensionFactoryRegistry::Add<wolf::WolfPipelineExtensionFactory>
    wolfPipelineExtensionRegistration(
        srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                        wolf::Api::Language::L1::WolfPipelineExecInstance>::ID,
        "");
