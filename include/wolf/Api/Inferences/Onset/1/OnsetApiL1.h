#ifndef WOLF_API_ONSETAPIL1_H
#define WOLF_API_ONSETAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

namespace wolf::Api::Onset::L1 {

    /// Identifies the phoneme onset inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.inference.Onset";

    /// Identifies Level 1 of the phoneme onset inference contract.
    inline constexpr int API_LEVEL = 1;

    /// What one Onset module recognizes.
    ///
    /// Onset does not take part in notation matching, so it declares no languages key.
    class OnsetExports : public srt::ContribExports {
    public:
        explicit OnsetExports(std::string variant)
            : ContribExports(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// Phonemes this module recognizes and matches on. This is a recognition set, not an
        /// output set, and it is a lower bound: a wildcard rule covers input that is not listed
        /// here, so under-reporting is not a defect. May be empty.
        std::vector<std::string> knownPhonemes;
    };

    class OnsetImportOptions : public srt::ContribImportOptions {
    public:
        explicit OnsetImportOptions(std::string variant)
            : ContribImportOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }
    };

    class OnsetRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        explicit OnsetRuntimeOptions(std::string variant)
            : InferenceRuntimeOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }
    };

    class OnsetInitArgs : public srt::InferenceInitArgs {
    public:
        OnsetInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// One batch of phoneme sequences.
    class OnsetStartInput : public srt::TaskStartInput {
    public:
        OnsetStartInput() : TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::vector<std::string>> phonemes;
    };

    /// One onset marker per input phoneme, each sequence the same length as its input.
    ///
    /// A position no rule covers is marked false. That is the intended meaning rather than an
    /// error, which is why Level 1 defines no per unit error channel here either.
    class OnsetResult : public srt::TaskResult {
    public:
        OnsetResult() : TaskResult(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::vector<bool>> onsets;
    };

    class OnsetExecutive : public srt::InferenceExecutive {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<OnsetResult>> result)>;

        virtual srt::Expected<void> initialize(const OnsetInitArgs &args) = 0;

        virtual srt::Expected<std::unique_ptr<OnsetResult>> start(const OnsetStartInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const OnsetStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // WOLF_API_ONSETAPIL1_H
