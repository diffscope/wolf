#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#include "PinyinInterpreter.h"

namespace wolf::pinyin {

    class PinyinPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet != srt::ContribTriplet(Api::G2p::L1::API_INTERFACE, Api::G2p::L1::API_LEVEL,
                                               VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new PinyinInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::pinyin::PinyinPlugin)
