#include <wolf/Language/LanguagePipelineExecutive.h>

namespace wolf {

    LanguagePipelineExecutive::LanguagePipelineExecutive(LanguageSpec &spec)
        : ContribExecutive(spec) {
    }

    LanguagePipelineExecutive::~LanguagePipelineExecutive() = default;

    srt::Expected<void> LanguagePipelineExecutive::quit() {
        return {};
    }

    srt::Expected<void> LanguagePipelineExecutive::wait() {
        return {};
    }

}
