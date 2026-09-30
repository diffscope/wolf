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
#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>
#include <synthrt/Task/ITask.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>
#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
// This is the only include from Api into the Linguist layer, and it is intentional. The inference
// contracts under Api/Inferences depend on synthrt's inference category alone, but this contract is
// defined on wolf's linguist category. LinguistExecutive derives from the executive base of that
// category, which carries the LinguistSpec that created the executive. Moving the base into Api
// would move a public class out of a header that hosts already include without changing any
// dependency.
#include <wolf/Linguist/LinguistPipelineExecutive.h>
#include <wolf/wolf_global.h>

namespace wolf::Api::Linguist::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.linguist.WolfLinguist";
    inline constexpr char API_VARIANT[] = "wolf";
    inline constexpr int API_LEVEL = 1;

    /// \name Import roles
    ///
    /// The three roles that a linguist composition of this contract binds, one per chain member.
    /// G2P is required. S2P and Onset are optional and determine the maximum depth of the
    /// composition (Depth). A role outside these three is not bound.
    ///
    /// The scripts under scripts/ define the same names as module constants (check-declarations.py,
    /// make-voicebank-fixture.py, convert-g2p-packages.py), and each script documents that its
    /// constants mirror these.
    /// \{
    inline constexpr char ROLE_G2P[] = "linguist/g2p";
    inline constexpr char ROLE_S2P[] = "linguist/s2p";
    inline constexpr char ROLE_ONSET[] = "linguist/onset";
    /// \}

    /// The phoneme inventory declared by one linguist composition.
    class LinguistExports : public srt::ContribExports {
    public:
        LinguistExports() : ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        std::vector<std::string> phonemes;

        /// Whether the composition may produce phonemes outside that list. The default is false.
        ///
        /// The list is required. For the twelve shipped chains the list is incomplete, because
        /// each chain ends with a fallback to the original word and its output set is therefore
        /// unbounded. This flag allows a host to report that the language may produce phonemes
        /// outside the voicebank inventory, instead of reporting nothing or relying on an
        /// inaccurate set.
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
    /// Level 1 defines no vocabulary. The composition already declares its pair, and the import
    /// therefore does not repeat it.
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

    /// Chain stage at which a conversion stops.
    ///
    /// The depth is a single cut instead of a set of switches, because onsets align one to one
    /// with phonemes and a request for onsets without phonemes is not a valid state.
    ///
    /// A composition may reach less than the requested depth. A composition without an S2P member
    /// reaches Pronunciation, and a composition without an Onset member reaches Phonemes. This is
    /// a property of the composition, not a failure, and a host reads it from the session before
    /// converting instead of detecting it in the output.
    enum class Depth {
        Pronunciation,
        Phonemes,
        Onsets,
    };

    /// Stage that produced a word. The value is diagnostic only.
    ///
    /// The first four values mirror the optional G2P hit source. Locked marks a word that passed
    /// through a layer pinned by the user.
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
    /// Phonemes and onsets are grouped because they must have the same length, and separate
    /// optionals could express an invalid state.
    struct LockedPhonemes {
        std::vector<std::string> phonemes;
        std::vector<bool> onsets;
    };

    /// One word to convert.
    ///
    /// A pinned layer is expressed by the presence of the optional instead of a companion flag,
    /// which distinguishes an unpinned layer from a layer pinned to an empty value.
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

    /// Conversion result of one word. A failed word keeps its position in the batch.
    struct LinguistWordOutput {
        std::string pronunciation;
        std::vector<std::string> candidates;

        G2P::L1::Mode mode = G2P::L1::Mode::Convert;
        G2P::L1::Error error = G2P::L1::Error::None;

        /// Present according to the requested depth.
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
    /// One executive runs one conversion at a time, which matches the limit of the inference
    /// executives that it supervises. A host that requires k concurrent conversions creates k
    /// executives.
    ///
    /// \par Stopping
    /// A stopped conversion is not a failure. stop() requests that the running conversion, or the
    /// next conversion if none is running, end at the next word or stage boundary. start() and the
    /// asynchronous callback then return the words finished so far, in their positions, and
    /// state() returns Canceled. Words that the conversion did not reach are returned in their
    /// unconverted form. A caller distinguishes a stopped conversion from a completed conversion by
    /// state(), not by an error.
    ///
    /// The G2P, S2P and Onset executives follow the same rule, and wolf::LinguistSession handles a
    /// cancelled CancelToken in the same way.
    ///
    /// This behavior intentionally differs from analysis executives built on synthrt, which report
    /// a stop as an error from start(). A linguist batch is a sequence of independent words, and
    /// the converted words remain correct if the remaining words are not converted. An editor
    /// displays them instead of discarding them. A partially computed analysis result is not a
    /// shorter correct result, and an error is therefore appropriate for analysis executives but
    /// not for linguist executives.
    class LinguistExecutive : public wolf::LinguistPipelineExecutive {
    public:
        using AsyncCallback =
            std::function<void(srt::Expected<std::unique_ptr<LinguistConvertResult>> result)>;

        /// The contract defines no initialize(), because every binding of this executive is fixed
        /// at creation and Level 1 defines no initialization arguments.
        virtual srt::Expected<std::unique_ptr<LinguistConvertResult>>
            start(const LinguistConvertInput &input) = 0;

        virtual srt::Expected<void> startAsync(std::shared_ptr<const LinguistConvertInput> input,
                                               AsyncCallback callback) = 0;

        virtual srt::ITask::State state() const noexcept = 0;
        virtual srt::Expected<void> stop() = 0;
        virtual srt::Expected<void> waitForFinished() = 0;

        /// \name Executive level diagnostics
        ///
        /// Both values are fixed for the lifetime of this executive and are therefore queried here
        /// instead of being copied into every word.
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

    /// Aggregates the linguist contributions that one singer declares.
    class WolfPipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        ~WolfPipelineExecutive() = default;

        /// Returns the language handles that this singer declares, in language map order.
        virtual const std::vector<std::string> &languages() const = 0;

        /// Creates the executive for one language.
        ///
        /// The key is the language handle instead of the import role, because a role is a local
        /// slot and hosts address languages. A handle that this singer does not declare results in
        /// InvalidArgument.
        ///
        /// \warning The returned executive is a child of this pipeline, and the pipeline retains it
        /// until the caller deletes it. Deleting the pointer detaches and destroys the executive,
        /// and no other operation destroys it before the pipeline is destroyed. Every call creates
        /// a new executive, and a caller that never deletes the returned executives therefore
        /// accumulates executives, together with their dictionaries and models, for the lifetime of
        /// the pipeline. createOwnedLinguist() is preferred because it expresses the ownership in
        /// the type.
        virtual srt::Expected<LinguistExecutive *>
            createLinguist(std::string_view language,
                           const LinguistRuntimeOptions &runtimeOptions) = 0;

        /// Creates the executive for one language and hands its ownership to the caller.
        ///
        /// The executive is the one that createLinguist() creates, held by a pointer that detaches
        /// and destroys it on destruction. The pointer must be released before this pipeline is
        /// destroyed, because destroying the pipeline destroys every attached executive, and a
        /// pointer that outlives the pipeline would delete an executive a second time.
        srt::Expected<std::unique_ptr<LinguistExecutive>>
            createOwnedLinguist(std::string_view language,
                                const LinguistRuntimeOptions &runtimeOptions) {
            auto created = createLinguist(language, runtimeOptions);
            if (!created) {
                return created.takeError();
            }
            return std::unique_ptr<LinguistExecutive>(created.take());
        }

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

    /// Creates a wolf linguist pipeline from the language map read during Package Load.
    class WolfPipelineExtension : public srt::SingerPipelineExtension {
    public:
        ~WolfPipelineExtension() = default;

        /// Returns the wolf pipeline extension of \a singer, or nullptr if the singer carries none.
        ///
        /// This function combines two steps: finding the extension registered under the identifier
        /// of the pipeline executive and casting it to this type. The cast is unchecked in the same
        /// way as ContribSpecExtension::as(), and the identifier establishes the type. A singer
        /// carries no extension if it declares no language that resolves to a linguist
        /// contribution.
        static WolfPipelineExtension *from(const srt::SingerSpec &singer);

        virtual const std::vector<std::string> &languages() const = 0;

        /// Returns the default language handle of this singer, or an empty string if none is
        /// declared.
        ///
        /// The value has no effect on loading or at run time. It is a hint from the author to the
        /// host.
        virtual const std::string &defaultLanguage() const = 0;

        /// Locates the linguist contribution behind a handle, for display and diagnostics.
        ///
        /// Returns nullptr for a handle that this singer does not declare. The locator belongs to
        /// the spec of the linguist and remains valid under the same conditions as exports().
        virtual const srt::ContribLocator *locate(std::string_view language) const = 0;

        /// Returns the pair to which the linguist behind a handle is bound.
        ///
        /// The pair is queried here instead of by resolving the result of locate(), because that
        /// locator belongs to the package of the linguist and cannot be resolved from the package
        /// of the singer. The binding is read together with the manifest, and this query therefore
        /// creates no object.
        ///
        /// Returns nullptr for a handle that this singer does not declare.
        virtual const Common::L1::LanguageScheme *binding(std::string_view language) const = 0;

        /// Returns the deepest layer that the composition behind a handle can reach.
        ///
        /// The value is derived from the import set, and this query therefore creates no object.
        /// A composition without an S2P member reaches Pronunciation, and a composition without an
        /// Onset member reaches Phonemes. A host queries the depth before converting instead of
        /// inferring the limit from incomplete output.
        ///
        /// Returns Pronunciation for a handle that this singer does not declare. locate()
        /// distinguishes that case.
        virtual Depth maxDepth(std::string_view language) const = 0;

        /// Returns the phoneme inventory that the composition behind a handle declares, together
        /// with the flag that indicates whether the list is complete.
        ///
        /// The inventory is queried here for the same reason as binding(): the declaration resides
        /// in the package of the linguist and cannot be resolved from the package of the singer.
        ///
        /// Returns nullptr for a handle that this singer does not declare.
        ///
        /// The pointer refers to the exports of the linguist spec, which may belong to another
        /// package, and is not a copy. It remains valid while a PackageHandle retains the package
        /// of the singer, because that package keeps its resolved dependencies loaded and the
        /// linguist belongs either to that package or to one of those dependencies. A pointer kept
        /// past the last handle, or across an unload of the package of the singer, is dangling.
        /// Callers that require the values for longer copy them, as the session catalog does.
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

namespace wolf::Api::Linguist::L1 {

    // Defined after the traits specialization above, which the lookup instantiates.
    inline WolfPipelineExtension *WolfPipelineExtension::from(const srt::SingerSpec &singer) {
        auto base = srt::ContribSpecExtension::findFromSpec<WolfPipelineExecutive>(singer);
        return base ? base->as<WolfPipelineExtension>() : nullptr;
    }

}

#endif // WOLF_API_LINGUISTAPIL1_H
