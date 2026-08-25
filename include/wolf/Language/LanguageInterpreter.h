#ifndef WOLF_LANGUAGEINTERPRETER_H
#define WOLF_LANGUAGEINTERPRETER_H

#include <synthrt/Core/ContribInterpreter.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Interprets language contributions and their imported execution stages.
    class WOLF_EXPORT LanguageInterpreter : public srt::ContribInterpreter {
    public:
        ~LanguageInterpreter() = default;

        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;

        srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
            createImportBinding(srt::ContribSpec &importer, const srt::ContribImport &declaration,
                                srt::ContribSpec &target,
                                std::unique_ptr<srt::ContribImportOptions> options) const override;

    protected:
        LanguageInterpreter() = default;
    };

}

#endif // WOLF_LANGUAGEINTERPRETER_H
