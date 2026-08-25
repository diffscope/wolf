#ifndef WOLF_LANGUAGEPIPELINEEXECUTIVE_H
#define WOLF_LANGUAGEPIPELINEEXECUTIVE_H

#include <synthrt/Core/ContribExecutive.h>

#include <wolf/Language/LanguageContrib.h>
#include <wolf/wolf_global.h>

namespace wolf {

    /// The provider-defined processing pipeline of one loaded language contribution.
    class WOLF_EXPORT LanguagePipelineExecutive : public srt::ContribExecutive {
    public:
        explicit LanguagePipelineExecutive(LanguageSpec &spec);
        ~LanguagePipelineExecutive();

        inline LanguageSpec &spec() const {
            return *ContribExecutive::spec().as<LanguageSpec>();
        }

    protected:
        /// Stops runtime activity retained by the language pipeline.
        srt::Expected<void> quit() override;

        /// Waits for runtime activity retained by the language pipeline.
        srt::Expected<void> wait() override;
    };

}

#endif // WOLF_LANGUAGEPIPELINEEXECUTIVE_H
