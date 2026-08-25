#ifndef WOLF_LANGUAGEPIPELINEEXECINSTANCE_H
#define WOLF_LANGUAGEPIPELINEEXECINSTANCE_H

#include <synthrt/Core/ContribExecInstance.h>

#include <wolf/Language/LanguageContrib.h>
#include <wolf/wolf_global.h>

namespace wolf {

    /// The provider-defined processing pipeline of one loaded language contribution.
    class WOLF_EXPORT LanguagePipelineExecInstance : public srt::ContribExecInstance {
    public:
        explicit LanguagePipelineExecInstance(LanguageSpec &spec);
        ~LanguagePipelineExecInstance();

        inline LanguageSpec &spec() const {
            return *ContribExecInstance::spec().as<LanguageSpec>();
        }

    protected:
        /// Stops runtime activity retained by the language pipeline.
        srt::Expected<void> quit() override;

        /// Waits for runtime activity retained by the language pipeline.
        srt::Expected<void> wait() override;
    };

}

#endif // WOLF_LANGUAGEPIPELINEEXECINSTANCE_H
