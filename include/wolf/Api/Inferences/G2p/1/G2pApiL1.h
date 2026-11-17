#ifndef WOLF_API_INFERENCES_G2P_1_G2PAPIL1_H
#define WOLF_API_INFERENCES_G2P_1_G2PAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf::Api::G2p::L1 {

    /// Identifies the G2p interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.wolf.inference.G2P", 1};

    /// Source of a converted word. The value is diagnostic only. A module that cannot determine
    /// the source, or that did not convert the word, leaves it Unspecified, and callers must not
    /// depend on it being set.
    enum class HitSource {
        Unspecified,
        Dict,
        Model,
        Rule,
        Fallback,
    };

    /// Production mode of a word result.
    enum class Mode {
        Convert,
        Copy,
        Skip,
    };

    /// Failure reason of a word. None indicates success, and word level success is determined by
    /// this field alone.
    enum class Error {
        None,
        InvalidInput,
        ModelInferenceFailed,
        PhonemeGenerationFailed,
        DriverUnavailable,
        NotInitialized,
        UnknownError,
    };

    /// Output declaration of one G2P module.
    ///
    /// All implementations of this contract share the same exports identity.
    class Exports : public srt::ContribExports {
    public:
        Exports() : ContribExports(Contract) {
        }

        /// Pairs for which this module produces pronunciations. The list may be empty. An empty
        /// list disables the static match, and the host is expected to issue a warning.
        std::vector<Common::L1::LanguageScheme> languages;

        /// The atomic symbols this module declares it may output. May be empty.
        std::vector<std::string> symbols;

        /// Whether output may fall outside symbols. The default is false, which indicates that
        /// symbols is the complete output set.
        ///
        /// A chain whose last step outputs the original word if no step matched must set this
        /// flag, because its output set is unbounded and symbols lists only the known part.
        /// Without the flag such a module could only leave symbols empty, and a host could not
        /// distinguish an empty declaration from an unbounded output set.
        bool openSet = false;
    };

    /// Level 1 defines no import options vocabulary. An executive is bound to one pair at
    /// creation, and the manifest therefore does not repeat the pair.
    class ImportOptions : public srt::ContribImportOptions {
    public:
        ImportOptions() : ContribImportOptions(Contract) {
        }
    };

    /// Settings supplied when a G2P executive is created.
    class RuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        RuntimeOptions() : InferenceRuntimeOptions(Contract) {
        }

        /// Pair to which this executive is bound for its whole lifetime. Runtime input carries no
        /// language, and a host that requires several languages therefore creates several
        /// executives.
        Common::L1::LanguageScheme binding;
    };

    class InitArgs : public srt::InferenceInitArgs {
    public:
        InitArgs() : InferenceInitArgs(Contract.interfaceId(), Contract.level()) {
        }
    };

    /// One batch of words to convert.
    class StartInput : public srt::TaskStartInput {
    public:
        StartInput() : TaskStartInput(Contract.interfaceId(), Contract.level()) {
        }

        std::vector<std::string> lyrics;
    };

    /// Conversion result of one word. A failed word keeps its position in the batch.
    struct WordOutput {
        std::string pronunciation;

        /// Alternatives, the first of which is the main pronunciation.
        std::vector<std::string> candidates;

        Mode mode = Mode::Convert;
        Error error = Error::None;
        HitSource hitSource = HitSource::Unspecified;
    };

    class Result : public srt::TaskResult {
    public:
        Result() : TaskResult(Contract.interfaceId(), Contract.level()) {
        }

        std::vector<WordOutput> words;
    };

    /// Executes one G2P module.
    ///
    /// One executive runs at most one conversion at a time, as srt::InferenceExecutive specifies.
    /// Concurrency requires additional executives.
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

#endif // WOLF_API_INFERENCES_G2P_1_G2PAPIL1_H
