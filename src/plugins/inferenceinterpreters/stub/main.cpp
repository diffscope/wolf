#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>

#include "StubExecutives.h"

namespace wolf::stub {

    class StubInferencePlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName == Api::G2P::L1::API_INTERFACE && level == Api::G2P::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubG2PInterpreter(std::string(variant)));
            }
            if (interfaceName == Api::S2P::L1::API_INTERFACE && level == Api::S2P::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubS2PInterpreter(std::string(variant)));
            }
            if (interfaceName == Api::Onset::L1::API_INTERFACE &&
                level == Api::Onset::L1::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(
                    new StubOnsetInterpreter(std::string(variant)));
            }
            return srt::Error(srt::Error::InvalidArgument,
                              "unsupported inference interpreter contract");
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::stub::StubInferencePlugin)
