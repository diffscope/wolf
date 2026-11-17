#include <memory>
#include <string>
#include <string_view>

#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>

#include "S2pInterpreter.h"

namespace S2pApi = wolf::Api::S2p::L1;

namespace wolf::s2p {

    class S2pPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet.interfaceId() != S2pApi::Contract.interfaceId() ||
                triplet.level() != S2pApi::Contract.level()) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported S2P contract: " + triplet.toString());
            }
            if (triplet.variant() != DirectTriplet.variant() &&
                triplet.variant() != DictTriplet.variant() &&
                triplet.variant() != MappingTriplet.variant()) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported S2P variant: " + triplet.variant());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new S2pInterpreter(triplet.variant()));
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::s2p::S2pPlugin)
