#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>

#include "OnsetInterpreter.h"

namespace wolf::onset {

    class OnsetPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != srt::ContribContractTriplet(Api::Onset::L1::API_INTERFACE,
                                                       Api::Onset::L1::API_LEVEL, RULE)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new OnsetInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::onset::OnsetPlugin)
