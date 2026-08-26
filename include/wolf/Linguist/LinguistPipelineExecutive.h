#ifndef WOLF_LINGUISTPIPELINEEXECUTIVE_H
#define WOLF_LINGUISTPIPELINEEXECUTIVE_H

#include <synthrt/Core/ContribExecutive.h>

#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/wolf_global.h>

namespace wolf {

    /// The provider-defined processing pipeline of one loaded linguist contribution.
    class WOLF_EXPORT LinguistPipelineExecutive : public srt::ContribExecutive {
    public:
        explicit LinguistPipelineExecutive(LinguistSpec &spec);
        ~LinguistPipelineExecutive();

        inline LinguistSpec &spec() const {
            return *ContribExecutive::spec().as<LinguistSpec>();
        }

    protected:
        /// Stops runtime activity retained by the linguist pipeline.
        srt::Expected<void> quit() override;

        /// Waits for runtime activity retained by the linguist pipeline.
        srt::Expected<void> wait() override;
    };

}

#endif // WOLF_LINGUISTPIPELINEEXECUTIVE_H
