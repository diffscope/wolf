#ifndef WOLF_API_LINGUISTAPIL1_H
#define WOLF_API_LINGUISTAPIL1_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/ContribSpec.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>
#include <synthrt/Task/ITask.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>
#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Linguist/LinguistPipelineExecutive.h>
#include <wolf/wolf_global.h>

namespace wolf::Api::Linguist::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.linguist.WolfLinguist";
    inline constexpr char API_VARIANT[] = "wolf";
    inline constexpr int API_LEVEL = 1;

    /// The phoneme inventory declared by one linguist composition.
    class LinguistExports : public srt::ContribExports {
    public:
        LinguistExports() : ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        std::vector<std::string> phonemes;

        /// Whether the composition may produce phonemes outside that list. Default false.
        ///
        /// The list is required, and for the twelve chains shipped today it is a suggestion
        /// rather than a fact: each ends by falling back to the word itself, so the output is
        /// unbounded. This is the bit that lets a host say so out loud — "this language may
        /// produce phonemes your voicebank does not know" — instead of either staying silent or
        /// stating a set that is not true.
        bool openSet = false;
    };

    /// The wolf variant configuration for a linguist composition.
    class LinguistConfiguration : public srt::ContribConfiguration {
    public:
        LinguistConfiguration() : ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Options supplied when another contribution imports a linguist composition.
    ///
    /// Level 1 defines no vocabulary: the composition already declares which pair it is, so
    /// nothing has to be repeated at the import.
    class LinguistImportOptions : public srt::ContribImportOptions {
    public:
        LinguistImportOptions() : ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Supplies runtime settings when a linguist executive is created.
    class LinguistRuntimeOptions : public srt::ContribRuntimeOptions {
    public:
        LinguistRuntimeOptions() : ContribRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Where in the chain a conversion stops.
    ///
    /// One cut rather than a set of switches, because onsets align one to one with phonemes and
    /// asking for onsets without phonemes is not a legal state to begin with.
    ///
    /// A composition may also be shallower than the depth asked for: one without an S2P member
    /// reaches Pronunciation, one without an Onset member reaches Phonemes. That is a property of
    /// the composition rather than a failure, and a host reads it from the session before
    /// converting instead of discovering it in the output.
    enum class Depth {
        Pronunciation,
        Phonemes,
        Onsets,
    };

    /// Which stage produced a word, for diagnostics only.
    ///
    /// The first four mirror the optional G2P hit source; Locked marks a word that passed through
    /// a layer the user had pinned.
    enum class HitStage {
        Unspecified,
        Dict,
        Model,
        Rule,
        Fallback,
        Locked,
    };

    /// A phoneme layer the user pinned by hand, with its onsets.
    ///
    /// The two travel together because they must be the same length, so separate optionals could
    /// express a state that is never valid.
    struct LockedPhonemes {
        std::vector<std::string> phonemes;
        std::vector<bool> onsets;
    };

    /// One word to convert.
    ///
    /// A pinned layer is expressed by the optional being present rather than by a companion flag,
    /// which keeps "not pinned" and "pinned to nothing" apart.
    struct LinguistWordInput {
        std::string lyric;
        std::optional<std::string> pronunciation;
        std::optional<LockedPhonemes> locked;
    };

    class LinguistConvertInput : public srt::TaskStartInput {
    public:
        LinguistConvertInput() : TaskStartInput(API_INTERFACE, API_LEVEL) {
        }

        std::vector<LinguistWordInput> words;
        Depth depth = Depth::Onsets;
    };

    /// What one word converted to. A failed word keeps its position in the batch.
    struct LinguistWordOutput {
        std::string pronunciation;
        std::vector<std::string> candidates;

        G2P::L1::Mode mode = G2P::L1::Mode::Convert;
        G2P::L1::Error error = G2P::L1::Error::None;

        /// Granted by depth.
        std::vector<std::string> phonemes;
        std::vector<bool> onsets;

        HitStage hitStage = HitStage::Unspecified;
    };

    class LinguistConvertResult : public srt::TaskResult {
    public:
        LinguistConvertResult() : TaskResult(API_INTERFACE, API_LEVEL) {
        }

        std::vector<LinguistWordOutput> words;
    };

    /// Owns the runtime activity of one linguist contribution.
    ///
    /// One executive carries one conversion at a time, matching the inference executives it
    /// supervises, which can carry no more than that either. A host that wants k conversions in
    /// flight creates k executives.
    class LinguistExecutive : public wolf::LinguistPipelineExecutive {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<LinguistConvertResult>> result)>;

        /// There is no initialize(): everything this executive binds to was fixed when it was
        /// created, and Level 1 has no second set of arguments to carry.
        virtual srt::Expected<std::unique_ptr<LinguistConvertResult>>
            start(const LinguistConvertInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const LinguistConvertInput> input,
                                               AsyncCallback callback) = 0;

        virtual srt::ITask::State state() const noexcept = 0;
        virtual srt::Expected<void> stop() = 0;
        virtual srt::Expected<void> waitForFinished() = 0;

        /// \name Executive level diagnostics
        ///
        /// Both are fixed for this executive's lifetime, so they are asked for here rather than
        /// copied onto every word.
        /// \{
        virtual const Common::L1::LanguageScheme &binding() const noexcept = 0;
        virtual const srt::ContribLocator &g2pContribution() const noexcept = 0;
        /// \}

    protected:
        using LinguistPipelineExecutive::LinguistPipelineExecutive;
    };

    /// Supplies runtime settings when the wolf linguist pipeline is created.
    class WolfPipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        WolfPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Aggregates the linguist contributions one singer declares.
    class WolfPipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        ~WolfPipelineExecutive() = default;

        /// Language handles this singer declares, in the order its language map yields them.
        virtual const std::vector<std::string> &languages() const = 0;

        /// Creates the executive for one language.
        ///
        /// Keyed by handle rather than by import role: a role is a local slot, and the host thinks
        /// in languages. A handle this singer does not declare is an InvalidArgument.
        virtual srt::Expected<LinguistExecutive *>
            createLinguist(std::string_view language,
                           const LinguistRuntimeOptions &runtimeOptions) = 0;

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

    /// Creates a wolf linguist pipeline from the language map read during Package Load.
    class WolfPipelineExtension : public srt::SingerPipelineExtension {
    public:
        ~WolfPipelineExtension() = default;

        virtual const std::vector<std::string> &languages() const = 0;

        /// The handle this singer names as its default, or empty when it names none.
        ///
        /// Carries no load or runtime meaning of its own; it is the author's hint to the host.
        virtual const std::string &defaultLanguage() const = 0;

        /// Locates the linguist contribution behind a handle, for display and diagnostics.
        ///
        /// Returns nullptr for a handle this singer does not declare.
        virtual const srt::ContribLocator *locate(std::string_view language) const = 0;

        /// The pair the linguist behind a handle is bound to.
        ///
        /// Asked here rather than by resolving locate()'s result, because that locator belongs to
        /// the linguist's own package and cannot be resolved from the singer's. The binding is
        /// read while the manifest is, so this answers without creating anything.
        ///
        /// Returns nullptr for a handle this singer does not declare.
        virtual const Common::L1::LanguageScheme *binding(std::string_view language) const = 0;

        /// The deepest layer the composition behind a handle can reach.
        ///
        /// Read from its import set, so this answers without creating anything: no S2P member
        /// means Pronunciation, no Onset member means Phonemes. A host asks before converting
        /// rather than inferring the limit from a short answer.
        ///
        /// Onsets for a handle this singer does not declare; ask locate() to tell that apart.
        virtual Depth maxDepth(std::string_view language) const = 0;

        /// The phoneme inventory the composition behind a handle declares, and whether that list
        /// is the whole of it.
        ///
        /// Asked here for the same reason binding() is: the declaration lives in the linguist's
        /// own package and cannot be resolved from the singer's.
        ///
        /// Returns nullptr for a handle this singer does not declare.
        virtual const LinguistExports *exports(std::string_view language) const = 0;

    protected:
        using SingerPipelineExtension::SingerPipelineExtension;
    };

}

namespace srt {

    template <>
    struct ContribSpecExtensionTraits<SingerSpec, wolf::Api::Linguist::L1::WolfPipelineExecutive> {
        inline static constexpr char ID[] = "org.openvpi.wolf.extension.LinguistPipeline";
    };

}

#endif // WOLF_API_LINGUISTAPIL1_H
