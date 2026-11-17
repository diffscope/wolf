#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#include "PinyinInterpreter.h"

namespace wolf::pinyin {

    class PinyinPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != Triplet) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new PinyinInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::pinyin::PinyinPlugin)
