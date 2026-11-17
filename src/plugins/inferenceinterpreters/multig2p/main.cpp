#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#include "MultiG2pInterpreter.h"

namespace wolf::multig2p {

    class MultiG2pPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != Triplet) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new MultiG2pInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::multig2p::MultiG2pPlugin)
