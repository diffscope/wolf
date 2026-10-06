#ifndef WOLF_LINGUISTSESSION_H
#define WOLF_LINGUISTSESSION_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Support/Expected.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/wolf_global.h>

namespace srt {
    class SynthUnit;
}

namespace wolf {

    /// Identifies one singer contribution to a session.
    ///
    /// A ContribLocator carries no version, and a host may load two versions of the same
    /// voicebank at once. The version is therefore stored beside the locator. An empty version
    /// selects the only loaded version. If several versions are loaded, an empty version is
    /// ambiguous and selects none, because an implicit choice would route the conversion to a
    /// voicebank that the host did not name.
    struct SingerRef {
        srt::ContribLocator locator;
        stdc::VersionNumber version;

        bool operator==(const SingerRef &other) const {
            return locator == other.locator && version == other.version;
        }
    };

    /// Current readiness of a (singer, language) pair. A query never changes it.
    enum class Readiness {
        Ready,       ///< Warmed. The next conversion loads no resources.
        Cold,        ///< A route exists and has not been tried.
        Unavailable, ///< Not usable. \c LanguageStatus::reason records the cause.
    };

    /// \note Cold does not guarantee that the resources load. It indicates only that no decision
    ///       remains: the binding is fixed, and the reachable depth and the coverage are already
    ///       computed. Whether a driver is present or a model opens is a property of the
    ///       installation and is determined only by an attempt, which warm() performs.
    struct LanguageStatus {
        Readiness readiness = Readiness::Unavailable;
        std::string reason;

        /// Kind of coverage of the declared language inventory by the phoneme table of the
        /// singer.
        enum class CoverageKind {
            /// No phoneme table was supplied for this singer, or the language declares no
            /// inventory. This value differs from zero coverage.
            Unknown,
            /// The language declares its inventory as complete, and the ratio is exact.
            Exact,
            /// The language declares an open set. Its list is a lower bound, and so is the ratio.
            Lower,
        };

        /// Deepest layer that this language can reach, independent of the depth that a
        /// conversion requests.
        ///
        /// The value is meaningful only if \c readiness is not \c Unavailable, because an
        /// unavailable language reaches no layer and no Depth value represents that state. The
        /// default is the shallowest layer instead of the deepest layer, so that a caller that
        /// reads the value without checking readiness requests less than is available instead of
        /// more than exists.
        ///
        /// The value equals \c LanguageEntry::maxDepth for the same language and is derived from
        /// the import set of the composition. It is repeated here because a caller of probe() does
        /// not necessarily hold the catalog entry, and neither query has a cost.
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Pronunciation;

        CoverageKind coverageKind = CoverageKind::Unknown;

        /// Value between 0 and 1, meaningful only if coverageKind is not Unknown.
        double coverage = 0.0;

        /// Declared phonemes that the singer cannot sing, truncated. The list serves as examples
        /// for display and is not complete.
        std::vector<std::string> missingPhonemes;
    };

    /// One language a singer declares.
    struct LanguageEntry {
        std::string handle;
        Api::Common::L1::LanguageScheme binding;
        srt::ContribLocator linguist;

        /// Deepest layer that this composition reaches, derived from its import set.
        ///
        /// The value equals the value that \c probe() reports for this language. A catalog entry
        /// exists only for a language that the singer declares, and the value is therefore always
        /// meaningful.
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Onsets;

        /// Phonemes that the composition declares as possible output, and whether the list is
        /// complete.
        std::vector<std::string> phonemes;
        bool openSet = false;
    };

    /// One singer, with the languages it declares.
    struct SingerEntry {
        SingerRef ref;
        std::vector<LanguageEntry> languages;

        /// Default language handle named by the author, or empty. The value is a hint for the
        /// host and takes no part in matching.
        std::string defaultLanguage;

        /// Markers that this singer declares (reservedPhonemes of the singer category). The
        /// session resolves these markers itself instead of passing them to a language. The list
        /// is empty if the singer declares none, and the session wide set then applies.
        std::vector<std::string> reservedMarkers;
    };

    /// Immutable snapshot of the unit contents at the time of construction.
    ///
    /// A snapshot may be held across a refresh. A later scan publishes a new catalog instead of
    /// modifying this one, and a snapshot therefore never changes while a host reads it.
    class WOLF_EXPORT LinguistCatalog {
    public:
        const std::vector<SingerEntry> &singers() const noexcept {
            return m_singers;
        }

        /// Finds a singer. An empty version in \a singer matches the only loaded version and
        /// matches no entry if several versions are loaded.
        const SingerEntry *find(const SingerRef &singer) const;

        /// Performs the same lookup and distinguishes the two causes of an empty result.
        /// \a ambiguous is set if several versions of the singer are loaded, and cleared if the
        /// singer is absent. The distinction matters for error reporting because the two causes
        /// require different corrections.
        const SingerEntry *find(const SingerRef &singer, bool &ambiguous) const;

    private:
        std::vector<SingerEntry> m_singers;

        friend class LinguistSession;
    };

    /// Handle that a host sets from another thread to end a conversion early.
    ///
    /// Cancellation requests that the executives currently lent out by the session stop. It does
    /// not guarantee that any particular word was or was not converted.
    class WOLF_EXPORT CancelToken {
    public:
        CancelToken();
        ~CancelToken();

        /// Copies share one state. A token passed to a worker therefore cancels the same
        /// conversion as the token that the caller retains.
        CancelToken(const CancelToken &other);
        CancelToken &operator=(const CancelToken &other);
        CancelToken(CancelToken &&other) noexcept;
        CancelToken &operator=(CancelToken &&other) noexcept;

        void cancel() noexcept;
        bool cancelled() const noexcept;

    private:
        class Impl;
        std::shared_ptr<Impl> m_impl;

        friend class LinguistSession;
    };

    /// Lifetime layer over the linguist domain: catalog, readiness and executive pool.
    ///
    /// Every host requires these functions and none of them depends on a host decision. They are
    /// therefore implemented here instead of once per host. Host decisions, such as threading,
    /// progress reporting and the notation of the project, are intentionally excluded.
    ///
    /// All members are thread safe.
    class WOLF_EXPORT LinguistSession {
    public:
        /// Borrows the unit instead of owning it, because a host usually uses other categories
        /// and services on the same unit. The unit must outlive the session.
        explicit LinguistSession(srt::SynthUnit &unit);

        /// \warning Every convert() call must have returned before the destructor runs. The
        ///          session owns the executives that a conversion uses and cannot wait for work
        ///          that it did not start. SynthUnit specifies the same rule for its handles.
        ~LinguistSession();

        /// Rebuilds the catalog from the packages committed in the unit and publishes it.
        ///
        /// Holders of the old catalog continue to see the old values. The readiness and failure
        /// caches are discarded, and the pool moves to a new generation. Idle entries are
        /// destroyed immediately, and lent entries are destroyed when they are returned, so that
        /// an executive is never destroyed during a conversion on another thread.
        ///
        /// The function returns no status, because a loaded package has already passed
        /// validation and a rescan of committed state has no failure path.
        void refresh();

        /// Discards the pool entries and the package handle of one singer and leaves other
        /// singers unchanged. A host calls this function before unloading the voicebank.
        ///
        /// The languages of the singer are Cold afterwards, because the next conversion loads the
        /// resources again. A cached failure is retained, because releasing resources does not
        /// repair a failed route. Lent entries are discarded when they are returned, as with
        /// refresh().
        void release(const SingerRef &singer);

        std::shared_ptr<const LinguistCatalog> catalog() const;

        /// Returns the status of a language without loading resources and without creating an
        /// executive.
        ///
        /// A singer that is loaded and declares languages but carries no linguist pipeline is
        /// absent from the catalog. In that case the reason names the route that was not mounted
        /// instead of reporting that the singer does not exist. This occurs if the languages of
        /// the singer route to modules that are not linguist contributions and no linguist
        /// provider was created in the load that brought the singer in, because the loader then
        /// has no validator for its language map. warm() and convert() report the same reason.
        ///
        /// A reason that comes from a recorded failure names the fault rather than restating its
        /// text: it carries the kind of the error code and every message of the cause chain, which
        /// are the parts that identify the layer that failed.
        LanguageStatus probe(const SingerRef &singer, std::string_view language) const;

        /// Creates the executive immediately and retains it, so that the next conversion loads no
        /// resources.
        ///
        /// A failure is cached with its reason until the next refresh(), so that one failure on a
        /// phrase of five hundred notes does not repeat five hundred times.
        srt::Expected<void> warm(const SingerRef &singer, std::string_view language);

        /// Performs one conversion. Depth, per-word pinning and per-word output use the L4 types.
        ///
        /// A batch that fails as a whole, meaning that the executive could not run it at all, is
        /// cached with its reason until the next refresh(), exactly as warm() caches a failure,
        /// because such a failure is a property of the route rather than of the words. probe()
        /// reports the route Unavailable afterwards. A per-word failure is a result of the
        /// conversion and is not cached.
        ///
        /// A cancelled \a token is not an error, according to the stopping rule of
        /// LinguistExecutive. The result holds the words finished before the cancellation took
        /// effect, and the words not reached are returned in their unconverted form. A caller
        /// distinguishes a cancelled conversion from a completed conversion by querying the
        /// token.
        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            convert(const SingerRef &singer, std::string_view language,
                    const Api::Linguist::L1::LinguistConvertInput &input,
                    const CancelToken &token = CancelToken());

        /// Sets the phonemes that a singer can sing, so that probe() can report coverage.
        ///
        /// The host supplies the table because wolf does not read voicebank formats. Without a
        /// table the coverage is Unknown, which intentionally differs from zero coverage, because
        /// a missing table and an empty coverage require different handling.
        ///
        /// The session applies no threshold, because a sufficient ratio is a product decision and
        /// the shipped languages range from full coverage to none. The only exception is zero
        /// coverage: a language whose inventory is declared complete and of which the singer
        /// covers no phoneme is reported Unavailable, because such a route is invalid rather than
        /// degraded.
        ///
        /// \a singer is looked up in the current catalog. A singer that the catalog does not
        /// hold, or of which it holds several versions if \a singer names no version, is ignored
        /// with a warning in the wolf log category. A table remains in effect until the singer
        /// leaves the catalog. refresh() discards the tables of singers that the new catalog no
        /// longer holds.
        void setSingerPhonemes(const SingerRef &singer, std::vector<std::string> phonemes);

        /// \name Reserved markers
        ///
        /// Markers that the session resolves itself instead of passing them to a language, for
        /// every singer that declares no set of its own. The set of a singer is the
        /// reservedPhonemes field of its declaration, a field of the singer category that synthrt
        /// reads and the singer validator checks against the models. The session takes that set
        /// from the catalog, and the host does not supply it. The session wide set follows the
        /// ecosystem convention for other singers, and a host that uses another notation can
        /// replace it. The default is SP and AP.
        ///
        /// A word on which the host pinned a pronunciation or a phoneme layer is never compared
        /// against this set, because pinning expresses explicit intent and the session must not
        /// overwrite it with the marker output.
        /// \{
        std::vector<std::string> reservedMarkers() const;
        void setReservedMarkers(std::vector<std::string> markers);
        /// \}

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;

        LinguistSession(const LinguistSession &) = delete;
        LinguistSession &operator=(const LinguistSession &) = delete;
    };

}

#endif // WOLF_LINGUISTSESSION_H
