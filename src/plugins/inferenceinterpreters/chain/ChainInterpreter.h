#ifndef WOLF_INFERENCEINTERPRETERS_CHAIN_CHAININTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_CHAIN_CHAININTERPRETER_H

#include <memory>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

namespace wolf::chain {

    inline const srt::ContribContractTriplet Triplet{Api::G2p::L1::Contract, "pipe-chain"};

    /// Interprets G2P modules of the pipe-chain variant, which run their steps in order.
    class ChainInterpreter : public srt::InferenceInterpreter {
    public:
        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_CHAIN_CHAININTERPRETER_H
