#include <wolf/Linguist/LinguistPipelineExecutive.h>

namespace wolf {

    LinguistPipelineExecutive::LinguistPipelineExecutive(LinguistSpec &spec)
        : ContribExecutive(spec) {
    }

    LinguistPipelineExecutive::~LinguistPipelineExecutive() = default;

    srt::Expected<void> LinguistPipelineExecutive::quit() {
        return {};
    }

    srt::Expected<void> LinguistPipelineExecutive::wait() {
        return {};
    }

}
