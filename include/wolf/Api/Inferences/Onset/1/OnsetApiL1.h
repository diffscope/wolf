#ifndef WOLF_API_INFERENCES_ONSET_1_ONSETAPIL1_H
#define WOLF_API_INFERENCES_ONSET_1_ONSETAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

namespace wolf::Api::Onset::L1 {

    /// Identifies the Onset interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.wolf.inference.Onset", 1};

    /// Recognition declaration of one Onset module.
    ///
    /// Onset does not take part in notation matching and therefore declares no languages key.
    class Exports : public srt::ContribExports {
    public:
        Exports() : ContribExports(Contract) {
        }

        /// Phonemes that this module recognizes and matches. This is a recognition set, not an
        /// output set, and it is a lower bound. A wildcard rule covers input that is not listed
        /// here, and an incomplete list is therefore not a defect. The list may be empty.
        std::vector<std::string> knownPhonemes;
    };

    class ImportOptions : public srt::ContribImportOptions {
    public:
        ImportOptions() : ContribImportOptions(Contract) {
        }
    };

    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        RuntimeOptions() : InferenceRuntimeOptions(Contract) {
        }
    };

    class InitArgs : public srt::InferenceInitArgs {
    public:
        InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// One batch of phoneme sequences.
    class StartInput : public srt::TaskStartInput {
    public:
        StartInput() : TaskStartInput(Contract.interfaceId(), Contract.level()) {
        }

        std::vector<std::vector<std::string>> phonemes;
    };

    /// One onset marker per input phoneme, each sequence the same length as its input.
    ///
    /// A position that no rule covers is marked false. This value is defined behavior, not an
    /// error, and Level 1 therefore defines no per unit error channel for Onset.
    class Result : public srt::TaskResult {
    public:
        Result() : TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        std::vector<std::vector<bool>> onsets;
    };

    class Executive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        virtual srt::Expected<void> initialize(const InitArgs &args) = 0;

        virtual srt::Expected<std::unique_ptr<Result>> start(const StartInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const StartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // WOLF_API_INFERENCES_ONSET_1_ONSETAPIL1_H
