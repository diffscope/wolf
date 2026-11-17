#ifndef WOLF_INFERENCEINTERPRETERS_ONSET_ONSETINTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_ONSET_ONSETINTERPRETER_H

#include <memory>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>

namespace wolf::onset {

    inline const srt::ContribContractTriplet Triplet{Api::Onset::L1::Contract, "rule"};

    /// Interprets Onset modules of the rule variant, which mark onsets from a rule file.
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

#endif // WOLF_INFERENCEINTERPRETERS_ONSET_ONSETINTERPRETER_H
