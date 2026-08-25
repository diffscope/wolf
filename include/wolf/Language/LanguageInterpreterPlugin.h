#ifndef WOLF_LANGUAGEINTERPRETERPLUGIN_H
#define WOLF_LANGUAGEINTERPRETERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

namespace wolf {

    /// The plugin extension point used by the wolf language category.
    class LanguageInterpreterPlugin : public srt::ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.wolf.plugin.LanguageInterpreter";

        ~LanguageInterpreterPlugin() = default;

    protected:
        LanguageInterpreterPlugin() = default;
    };

}

#endif // WOLF_LANGUAGEINTERPRETERPLUGIN_H
