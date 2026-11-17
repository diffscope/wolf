#ifndef WOLF_INFERENCEINTERPRETERS_LUA_LUAINTERPRETERS_H
#define WOLF_INFERENCEINTERPRETERS_LUA_LUAINTERPRETERS_H

#include <memory>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>

namespace wolf::lua {

    inline const srt::ContribContractTriplet S2pTriplet{Api::S2p::L1::Contract, "lua"};
    inline const srt::ContribContractTriplet OnsetTriplet{Api::Onset::L1::Contract, "lua"};

    /// Interprets S2P modules of the lua variant.
    class S2pInterpreter : public srt::InferenceInterpreter {
    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &) override;
    };

    /// Interprets Onset modules of the lua variant.
    class OnsetInterpreter : public srt::InferenceInterpreter {
    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &) override;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_LUA_LUAINTERPRETERS_H
