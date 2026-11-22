#ifndef WOLF_API_INFERENCES_S2P_1_S2PAPIL1_H
#define WOLF_API_INFERENCES_S2P_1_S2PAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf::Api::S2p::L1 {

    /// Identifies the S2p interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.wolf.inference.S2P", 1};

    /// Input and output declaration of one S2P module.
    class Exports : public srt::ContribExports {
    public:
        Exports() : ContribExports(Contract) {
        }

        /// Pairs whose pronunciations this module consumes. This key is the dual of the G2P key
        /// of the same name and is equally optional. See G2P for the empty list rule.
        std::vector<Common::L1::LanguageScheme> languages;

        /// Phonemes that this module declares as possible output. Reserved phonemes are
        /// excluded. The list may be empty.
        std::vector<std::string> phonemes;

        /// Whether output may fall outside phonemes. The default is false. A scripted variant
        /// whose output is determined by the script sets this flag instead of leaving the list
        /// empty, because an empty list indicates that no phonemes were declared.
        bool openSet = false;

        /// Whether every output preserves the input pronunciation units.
        /// Implementations set this capability. Unknown or conditional behavior uses false.
        bool preservesSymbols = false;
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

        /// See G2p::L1::RuntimeOptions::binding: the pair bound for the whole lifetime.
        Common::L1::LanguageScheme binding;
    };

    class InitArgs : public srt::InferenceInitArgs {
    public:
        InitArgs() : InferenceInitArgs(Contract) {
        }
    };

    /// One batch of pronunciation strings.
    ///
    /// A space is the reserved delimiter of the pronunciation layer. A string that contains a
    /// space is a space delimited sequence and is converted segment by segment. A string without
    /// a space is a single pronunciation unit.
    class StartInput : public srt::TaskStartInput {
    public:
        StartInput() : TaskStartInput(Contract) {
        }

        std::vector<std::string> pronunciations;
    };

    /// The phoneme sequence produced for each input, in the same order.
    ///
    /// Level 1 has no per unit error channel. An input without any match yields an empty
    /// sequence, which is not a failure. A batch that cannot proceed fails through the Expected
    /// returned by start().
    class Result : public srt::TaskResult {
    public:
        Result() : TaskResult(Contract) {
        }

        std::vector<std::vector<std::string>> phonemes;
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

#endif // WOLF_API_INFERENCES_S2P_1_S2PAPIL1_H
