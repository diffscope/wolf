#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageProviderPlugin.h>

#include "WolfLanguageProvider.h"

namespace wolf {

    class WolfLanguageProviderPlugin : public LanguageProviderPlugin {
    public:
        WolfLanguageProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Lang = Api::Language::L1;
            if (interfaceName != Lang::API_INTERFACE || level != Lang::API_LEVEL ||
                variant != Lang::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported language interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLanguageProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLanguageProviderPlugin)
