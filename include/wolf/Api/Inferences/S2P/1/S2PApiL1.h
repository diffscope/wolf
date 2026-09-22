#ifndef WOLF_API_S2PAPIL1_H
#define WOLF_API_S2PAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf::Api::S2P::L1 {

    /// Identifies the phonological symbol to phoneme inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.inference.S2P";

    /// Identifies Level 1 of the phonological symbol to phoneme inference contract.
    inline constexpr int API_LEVEL = 1;

    /// What one S2P module accepts and produces.
    class S2PExports : public srt::ContribExports {
    public:
        explicit S2PExports(std::string variant)
            : ContribExports(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// Pairs whose pronunciations this module can consume. Dual to the G2P key of the same
        /// name, and equally optional: leaving it empty forfeits the static match.
        std::vector<Common::L1::LanguageScheme> languages;

        /// The phonemes this module declares it may produce. Reserved phonemes are excluded.
        /// May be empty.
        std::vector<std::string> phonemes;

        /// Whether output may fall outside phonemes. Default false. A scripted variant whose
        /// output is whatever the script returns says true rather than leaving the list empty:
        /// empty says nothing was declared, which is a different statement.
        bool openSet = false;
    };

    class S2PImportOptions : public srt::ContribImportOptions {
    public:
        explicit S2PImportOptions(std::string variant)
            : ContribImportOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }
    };

    class S2PRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        explicit S2PRuntimeOptions(std::string variant)
            : InferenceRuntimeOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// The pair this executive is bound to for its whole lifetime.
        Common::L1::LanguageScheme binding;
    };

    class S2PInitArgs : public srt::InferenceInitArgs {
    public:
        S2PInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// One batch of pronunciation strings.
    ///
    /// A space is the reserved delimiter of the pronunciation layer: a string containing one is a
    /// space delimited sequence to convert segment by segment, and a string without one is a
    /// single pronunciation unit.
    class S2PStartInput : public srt::TaskStartInput {
    public:
        S2PStartInput() : TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::string> pronunciations;
    };

    /// The phoneme sequence produced for each input, in the same order.
    ///
    /// Level 1 has no per unit error channel. An input that matched nothing yields an empty
    /// sequence, which is not a failure; a batch that cannot proceed at all fails through the
    /// Expected returned by start().
    class S2PResult : public srt::TaskResult {
    public:
        S2PResult() : TaskResult(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::vector<std::string>> phonemes;
    };

    class S2PExecutive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<S2PResult>> result)>;

        virtual srt::Expected<void> initialize(const S2PInitArgs &args) = 0;

        virtual srt::Expected<std::unique_ptr<S2PResult>> start(const S2PStartInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const S2PStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // WOLF_API_S2PAPIL1_H
