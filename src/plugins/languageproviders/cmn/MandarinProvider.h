#ifndef WOLF_MANDARINPROVIDER_H
#define WOLF_MANDARINPROVIDER_H

#include <wolf/Language/LanguageProvider.h>

namespace wolf {

    class MandarinProvider : public LanguageProvider {
    public:
        MandarinProvider();
        ~MandarinProvider();

    public:
        int apiLevel() const override;

        srt::Expected<srt::UNO<LanguageSchema>>
            createSchema(const LanguageSpec *spec) const override;

        srt::Expected<srt::UNO<LanguageConfiguration>>
            createConfiguration(const LanguageSpec *spec) const override;
    };

}

#endif // WOLF_MANDARINPROVIDER_H
