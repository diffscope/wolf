#ifndef WOLF_LANGUAGEPROVIDERPLUGIN_H
#define WOLF_LANGUAGEPROVIDERPLUGIN_H

#include <synthrt/Plugin/Plugin.h>

#include <wolf/Language/LanguageProvider.h>

namespace wolf {

    class LanguageProviderPlugin : public srt::Plugin {
    public:
        LanguageProviderPlugin() = default;
        ~LanguageProviderPlugin() = default;

        static constexpr const char *IID = "org.openvpi.LanguageProvider";

        const char *iid() const override {
            return IID;
        }

    public:
        virtual srt::UNO<LanguageProvider> create() = 0;

    public:
        STDCORELIB_DISABLE_COPY(LanguageProviderPlugin)
    };

}

#endif // WOLF_LANGUAGEPROVIDERPLUGIN_H
