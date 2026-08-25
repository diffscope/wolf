#ifndef WOLF_WOLFLANGUAGEINTERPRETER_H
#define WOLF_WOLFLANGUAGEINTERPRETER_H

#include <wolf/Language/LanguageInterpreter.h>

namespace wolf {

    class WolfLanguageInterpreter : public LanguageInterpreter {
    public:
        WolfLanguageInterpreter();
        ~WolfLanguageInterpreter();

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;

        srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override;
    };

}

#endif // WOLF_WOLFLANGUAGEINTERPRETER_H
