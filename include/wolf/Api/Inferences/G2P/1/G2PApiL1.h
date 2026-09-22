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

    /// Where a converted word came from. Purely diagnostic: a module that cannot tell, or that did
    /// not convert, leaves it Unspecified, and no caller may depend on it being set.
    enum class HitSource {
        Unspecified,
        Dict,
        Model,
        Rule,
        Fallback,
    };

    /// How a word's result was produced.
    enum class Mode {
        Convert,
        Copy,
        Skip,
    };

    /// Why a word failed. None means success; word level success is decided by this field alone.
    enum class Error {
        None,
        InvalidInput,
        ModelInferenceFailed,
        PhonemeGenerationFailed,
        DriverUnavailable,
        NotInitialized,
        UnknownError,
    };

    /// What one G2P module can produce.
    ///
    /// Every payload of a multi variant contract carries the variant, because the loader compares
    /// the whole triple against the declaration that produced it.
    class G2PExports : public srt::ContribExports {
    public:
        explicit G2PExports(std::string variant)
            : ContribExports(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// Pairs this module can produce pronunciations for. May be empty, which forfeits the
        /// static match and leaves the host to warn.
        std::vector<Common::L1::LanguageScheme> languages;

        /// The atomic symbols this module declares it may output. May be empty.
        std::vector<std::string> symbols;

        /// Whether output may fall outside symbols. Default false, which means the list is the
        /// whole of it and reads exactly as it always has.
        ///
        /// True is what a chain ending in "output the word itself when nothing matched" has to
        /// say: its output is unbounded, so any list it gives is the part it knows. Without this
        /// such a module can only leave symbols empty, which says nothing at all — a host cannot
        /// tell "I declare nothing" from "I may produce anything".
        bool openSet = false;
    };

    /// Level 1 defines no import options vocabulary: an executive is bound to one pair when it is
    /// created, so nothing has to be written into the manifest twice.
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

        /// The pair this executive is bound to for its whole lifetime. Runtime input carries no
        /// language of its own, so a host that needs several languages creates several executives.
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

    /// What one word converted to. A failed word keeps its position in the batch.
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
    /// One executive carries one in flight conversion, which is what srt::InferenceExecutive
    /// exposes. Concurrency comes from creating more executives.
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
