#ifndef WOLF_LINGUISTPROVIDERPLUGIN_H
#define WOLF_LINGUISTPROVIDERPLUGIN_H

#include <synthrt/Core/ContribInterpreterPlugin.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Creates providers for supported linguist contracts.
    class WOLF_EXPORT LinguistProviderPlugin : public srt::ContribInterpreterPlugin {
    public:
        static constexpr const char *IID = "org.openvpi.wolf.plugin.LinguistProvider";

        ~LinguistProviderPlugin() = default;

    protected:
        LinguistProviderPlugin() = default;
    };

}

#endif // WOLF_LINGUISTPROVIDERPLUGIN_H
