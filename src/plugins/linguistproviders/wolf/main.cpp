#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#include "WolfLinguistProvider.h"

namespace wolf {

    class WolfLinguistProviderPlugin : public LinguistProviderPlugin {
    public:
        WolfLinguistProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace LinguistApi = Api::Linguist::L1;
            if (triplet != srt::ContribTriplet(LinguistApi::API_INTERFACE, LinguistApi::API_LEVEL,
                                               LinguistApi::API_VARIANT)) {
                return srt::Error(
                    srt::Error::InvalidArgument,
                    "unsupported linguist interpreter contract: " + triplet.interfaceId() + "/" +
                        std::to_string(triplet.level()) + "/" + triplet.variant());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLinguistProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLinguistProviderPlugin)
