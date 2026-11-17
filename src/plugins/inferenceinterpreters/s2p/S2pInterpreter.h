#ifndef WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H

#include <memory>
#include <string>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>

namespace wolf::s2p {

    inline const srt::ContribContractTriplet DirectTriplet{Api::S2p::L1::Contract, "direct"};

    inline const srt::ContribContractTriplet DictTriplet{Api::S2p::L1::Contract, "dict"};

    inline const srt::ContribContractTriplet MappingTriplet{Api::S2p::L1::Contract, "mapping"};

    /// Interprets S2P modules of the direct, dict and mapping variants. The variant is
    /// fixed when the interpreter is created.
    class S2pInterpreter : public srt::InferenceInterpreter {
    public:
        explicit S2pInterpreter(std::string variant);
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

    private:
        std::string m_variant;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H
