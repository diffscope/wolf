#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#include "WolfLinguistProvider.h"

#include <wolf/Api/Linguists/Linguist/1/LinguistWolfApiL1.h>

namespace wolf {

    class WolfLinguistProviderPlugin final : public LinguistProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != Api::Linguist::L1::Wolf::Triplet) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLinguistProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLinguistProviderPlugin)
