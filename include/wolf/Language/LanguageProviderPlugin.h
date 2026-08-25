#ifndef WOLF_LANGUAGEPROVIDERPLUGIN_H
#define WOLF_LANGUAGEPROVIDERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Creates providers for supported language contracts.
    class WOLF_EXPORT LanguageProviderPlugin : public srt::ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.wolf.plugin.LanguageProvider";

        ~LanguageProviderPlugin() = default;

    protected:
        LanguageProviderPlugin() = default;
    };

}

#endif // WOLF_LANGUAGEPROVIDERPLUGIN_H
