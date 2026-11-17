#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#include "MultiG2pInterpreter.h"

namespace wolf::multig2p {

    class MultiG2pPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet != srt::ContribTriplet(Api::G2p::L1::API_INTERFACE, Api::G2p::L1::API_LEVEL,
                                               VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new MultiG2pInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::multig2p::MultiG2pPlugin)
