#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#include "WolfLinguistProvider.h"

namespace wolf {

    class WolfLinguistProviderPlugin final : public LinguistProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet != srt::ContribTriplet(Api::Linguist::L1::API_INTERFACE,
                                               Api::Linguist::L1::API_LEVEL,
                                               Api::Linguist::L1::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLinguistProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLinguistProviderPlugin)
