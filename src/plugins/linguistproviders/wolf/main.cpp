#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#include "WolfLinguistProvider.h"

namespace wolf {

    class WolfLinguistProviderPlugin : public LinguistProviderPlugin {
    public:
        WolfLinguistProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace LinguistApi = Api::Linguist::L1;
            if (interfaceName != LinguistApi::API_INTERFACE || level != LinguistApi::API_LEVEL ||
                variant != LinguistApi::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported linguist interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLinguistProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLinguistProviderPlugin)
