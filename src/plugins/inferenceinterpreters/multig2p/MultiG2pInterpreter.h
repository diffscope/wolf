#ifndef WOLF_INFERENCEINTERPRETERS_MULTIG2P_MULTIG2PINTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_MULTIG2P_MULTIG2PINTERPRETER_H

#include <memory>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

namespace wolf::multig2p {

    inline const srt::ContribContractTriplet Triplet{Api::G2p::L1::Contract, "multig2p-onnx"};

    /// Interprets G2P modules of the multig2p-onnx variant, which run the shared model
    /// backend through the ONNX driver.
    class MultiG2pInterpreter : public srt::InferenceInterpreter {
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
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_MULTIG2P_MULTIG2PINTERPRETER_H
