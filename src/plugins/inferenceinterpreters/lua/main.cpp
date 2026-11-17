#include <memory>
#include <string>
#include <string_view>

#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>

#include "LuaInterpreters.h"

namespace S2pApi = wolf::Api::S2p::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::lua {

    class LuaPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet.variant() != S2pTriplet.variant()) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported variant: " + triplet.variant());
            }
            if (triplet.interfaceId() == S2pApi::Contract.interfaceId() &&
                triplet.level() == S2pApi::Contract.level()) {
                return std::unique_ptr<srt::ContribInterpreter>(new S2pInterpreter());
            }
            if (triplet.interfaceId() == OnsetApi::Contract.interfaceId() &&
                triplet.level() == OnsetApi::Contract.level()) {
                return std::unique_ptr<srt::ContribInterpreter>(new OnsetInterpreter());
            }
            return srt::Error(srt::Error::InvalidArgument, "unsupported scripted contract");
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::lua::LuaPlugin)
