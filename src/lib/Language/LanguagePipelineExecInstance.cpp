#include <wolf/Language/LanguagePipelineExecInstance.h>

namespace wolf {

    LanguagePipelineExecInstance::LanguagePipelineExecInstance(LanguageSpec &spec)
        : ContribExecInstance(spec) {
    }

    LanguagePipelineExecInstance::~LanguagePipelineExecInstance() = default;

    srt::Expected<void> LanguagePipelineExecInstance::quit() {
        return {};
    }

    srt::Expected<void> LanguagePipelineExecInstance::wait() {
        return {};
    }

}
