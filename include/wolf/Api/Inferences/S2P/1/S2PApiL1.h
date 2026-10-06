#ifndef WOLF_API_S2PAPIL1_H
#define WOLF_API_S2PAPIL1_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
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

    /// The variant whose output holds the symbols of its input.
    ///
    /// A pronunciation is already a space delimited sequence of pronunciation units (see
    /// S2PStartInput), and this variant copies every unit, so the layer it produces is the
    /// pronunciation layer rather than a layer of its own. The identity is a property of the
    /// symbols and not of the text: a run of spaces collapses into one unit and the spaces around
    /// the units are dropped, so the two layers hold the same units rather than the same string.
    ///
    /// The other variants of this contract rewrite the symbols of a pronunciation, through a
    /// dictionary entry, a mapping row or a script, so the two layers are distinct. A scripted
    /// variant, however, cannot be decided from its declaration: whether its output keeps the
    /// symbols is known only after it runs. Such a variant is reported as holding a layer of its
    /// own, which is the conservative answer: presenting a pronunciation layer that turns out to be
    /// the phoneme layer is a smaller error than hiding a layer that exists.
    ///
    /// A host must not treat this as an exhaustive list of the variants that keep the symbols. A
    /// third-party variant may keep them as well, and no version of this header can say so.
    inline constexpr char VARIANT_DIRECT[] = "direct";

    /// Whether \a variant converts a pronunciation without changing its symbols. See VARIANT_DIRECT.
    ///
    /// Only VARIANT_DIRECT is known to keep the symbols; every other name returns false. That
    /// includes a variant this version does not know, because a caller must not assume that an
    /// unknown variant keeps the symbols — it can assume the opposite, which is also the direction
    /// the report of a separate pronunciation layer takes for a variant whose output is not
    /// statically known.
    ///
    /// A host asks this about a declaration rather than about output, because it has to know the
    /// shape of the two layers before it converts: a conversion reports the symbols of one layer,
    /// not whether the other layer would hold the same ones.
    ///
    /// The packaging pass keeps its own list of the variants whose table it reads
    /// (TABLE_VARIANTS in scripts/check-declarations.py), and the variants guide states the same
    /// table in §5.0.2. A variant that reads a table must be added in all three places; no script
    /// cross-checks them against this function.
    inline constexpr bool variantKeepsSymbols(std::string_view variant) {
        return variant == VARIANT_DIRECT;
    }

    /// Input and output declaration of one S2P module.
    class S2PExports : public srt::ContribExports {
    public:
        explicit S2PExports(std::string variant)
            : ContribExports(API_INTERFACE, std::move(variant), API_LEVEL) {
        }

        /// Pairs whose pronunciations this module consumes. This key is the dual of the G2P key
        /// of the same name and is equally optional; see G2P for the empty list rule.
        std::vector<Common::L1::LanguageScheme> languages;

        /// Phonemes that this module declares as possible output. Reserved phonemes are
        /// excluded. The list may be empty.
        std::vector<std::string> phonemes;

        /// Whether output may fall outside phonemes. The default is false. A scripted variant
        /// whose output is determined by the script sets this flag instead of leaving the list
        /// empty, because an empty list indicates that no phonemes were declared.
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

        /// See G2P::L1::G2PRuntimeOptions::binding: the pair bound for the whole lifetime.
        Common::L1::LanguageScheme binding;
    };

    class S2PInitArgs : public srt::InferenceInitArgs {
    public:
        S2PInitArgs() : InferenceInitArgs(API_INTERFACE, API_LEVEL) {
        }
    };

    /// One batch of pronunciation strings.
    ///
    /// A space is the reserved delimiter of the pronunciation layer. A string that contains a
    /// space is a space delimited sequence and is converted segment by segment. A string without
    /// a space is a single pronunciation unit.
    class S2PStartInput : public srt::TaskStartInput {
    public:
        S2PStartInput() : TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::vector<std::string> pronunciations;
    };

    /// The phoneme sequence produced for each input, in the same order.
    ///
    /// Level 1 has no per unit error channel. An input without any match yields an empty
    /// sequence, which is not a failure. A batch that cannot proceed fails through the Expected
    /// returned by start().
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
