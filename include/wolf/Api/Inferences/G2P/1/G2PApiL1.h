#ifndef WOLF_API_G2PAPIL1_H
#define WOLF_API_G2PAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecutive.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf::Api::G2P::L1 {

    /// Identifies the grapheme to phonological symbol inference contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.inference.G2P";

    /// Identifies Level 1 of the grapheme to phonological symbol inference contract.
    inline constexpr int API_LEVEL = 1;

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
    /// Every payload of a multi variant contract carries the variant, because the loader compares
    /// the whole triple against the declaration that produced the payload.
    class G2PExports : public srt::ContribExports {
    public:
        explicit G2PExports(std::string variant)
            : ContribExports(API_INTERFACE, std::move(variant), API_LEVEL) {
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
    class G2PImportOptions : public srt::ContribImportOptions {
    public:
        explicit G2PImportOptions(std::string variant)
            : ContribImportOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }
    };

    /// Settings supplied when a G2P executive is created.
    class G2PRuntimeOptions : public srt::InferenceRuntimeOptions {
    public:
        explicit G2PRuntimeOptions(std::string variant)
            : InferenceRuntimeOptions(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// Pair to which this executive is bound for its whole lifetime. Runtime input carries no
        /// language, and a host that requires several languages therefore creates several
        /// executives.
        Common::L1::LanguageScheme binding;
    };

    class G2PInitArgs : public srt::InferenceInitArgs {
    public:
        G2PInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// One batch of words to convert.
    class G2PStartInput : public srt::TaskStartInput {
    public:
        G2PStartInput() : TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::string> lyrics;
    };

    /// Conversion result of one word. A failed word keeps its position in the batch.
    struct G2PWordOutput {
        std::string pronunciation;

        /// Alternatives, the first of which is the main pronunciation.
        std::vector<std::string> candidates;

        Mode mode = Mode::Convert;
        Error error = Error::None;
        HitSource hitSource = HitSource::Unspecified;
    };

    class G2PResult : public srt::TaskResult {
    public:
        G2PResult() : TaskResult(API_INTERFACE, API_LEVEL) {
        }

        std::vector<G2PWordOutput> words;
    };

    /// Executes one G2P module.
    ///
    /// One executive runs at most one conversion at a time, as srt::InferenceExecutive specifies.
    /// Concurrency requires additional executives.
    class G2PExecutive : public srt::InferenceExecutive {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<G2PResult>> result)>;

        virtual srt::Expected<void> initialize(const G2PInitArgs &args) = 0;

        virtual srt::Expected<std::unique_ptr<G2PResult>> start(const G2PStartInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const G2PStartInput> input,
                                               AsyncCallback callback) = 0;

    protected:
        using InferenceExecutive::InferenceExecutive;
    };

}

#endif // WOLF_API_G2PAPIL1_H
