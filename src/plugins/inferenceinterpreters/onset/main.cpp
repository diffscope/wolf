#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>

#include "OnsetInterpreter.h"

namespace wolf::onset {

    class OnsetPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != Triplet) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new OnsetInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::onset::OnsetPlugin)
