#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>

#include "StubExecutives.h"

namespace wolf::stub {

    class StubInferencePlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet.interfaceId() == Api::G2p::L1::API_INTERFACE &&
                triplet.level() == Api::G2p::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubG2pInterpreter(triplet.variant()));
            }
            if (triplet.interfaceId() == Api::S2p::L1::API_INTERFACE &&
                triplet.level() == Api::S2p::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubS2pInterpreter(triplet.variant()));
            }
            if (triplet.interfaceId() == Api::Onset::L1::API_INTERFACE &&
                triplet.level() == Api::Onset::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubOnsetInterpreter(triplet.variant()));
            }
            return srt::Error(srt::Error::InvalidArgument,
                              "unsupported inference interpreter contract");
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::stub::StubInferencePlugin)
