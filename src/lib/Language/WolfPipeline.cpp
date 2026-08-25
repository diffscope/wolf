#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

#include <memory>

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

    }

    srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
        createLanguageExecFactory(srt::ContribImportBinding &binding) {
        return std::unique_ptr<srt::ContribExecFactory>(new LanguageExecFactory(binding));
    }

}
