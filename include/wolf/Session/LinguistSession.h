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
    /// A ContribLocator carries no version, and a host may well have two versions of the same
    /// voicebank loaded at once, so the version rides alongside it. Leaving it empty means "the
    /// only loaded one", which is an ambiguity rather than a choice when there is more than one:
    /// picking for the host would silently route to a voicebank they did not name.
    struct SingerRef {
        srt::ContribLocator locator;
        stdc::VersionNumber version;

        bool operator==(const SingerRef &other) const {
            return locator == other.locator && version == other.version;
        }
    };

    /// What a (singer, language) pair can do right now. Asking never changes it.
    enum class Readiness {
        Ready,       ///< Warmed: the next conversion loads nothing.
        Cold,        ///< There is a route and it has not been tried.
        Unavailable, ///< Not usable here; reason says why.
    };

    /// \note Cold is not a promise that the resources load, only that nothing is left to decide:
    ///       the binding is fixed, the reachable depth and the coverage below are already
    ///       computed. Whether a driver is present or a model opens is a property of the
    ///       installation and is known only once it is tried, which is what warm() does.
    struct LanguageStatus {
        Readiness readiness = Readiness::Unavailable;
        std::string reason;

        /// How completely the singer's phoneme table covers what the language declares.
        enum class CoverageKind {
            /// No phoneme table was given for this singer, or the language declares no inventory.
            /// Not the same as zero coverage.
            Unknown,
            /// The language states its inventory is the whole of it, so the ratio is exact.
            Exact,
            /// The language declares an open set, so its list is a floor and so is this ratio.
            Lower,
        };

        /// The deepest layer this language can reach, whatever depth a conversion asks for.
        ///
        /// Meaningful only when \c readiness is not \c Unavailable: a language that is not there
        /// reaches nothing, and there is no depth that says so. The default is the shallowest
        /// rather than the deepest so that a caller which reads this without checking asks for
        /// less than it could rather than more than exists.
        ///
        /// The same value as \c LanguageEntry::maxDepth for the same language, read from the
        /// composition's import set. It is repeated here because a caller that probes does not
        /// necessarily hold the catalog entry, and asking costs nothing either way.
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Pronunciation;

        CoverageKind coverageKind = CoverageKind::Unknown;

        /// Between 0 and 1, meaningful only when coverageKind is not Unknown.
        double coverage = 0.0;

        /// Declared phonemes the singer cannot sing, truncated. A host showing this to a person
        /// needs examples, not the whole list.
        std::vector<std::string> missingPhonemes;
    };

    /// One language a singer declares.
    struct LanguageEntry {
        std::string handle;
        Api::Common::L1::LanguageScheme binding;
        srt::ContribLocator linguist;

        /// The deepest layer this composition reaches, from its import set.
        ///
        /// The same value \c probe() reports for this language. A catalog entry exists only for a
        /// language the singer declares, so here it is always meaningful.
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Onsets;

        /// What the composition declares it may produce, and whether that is the whole of it.
        std::vector<std::string> phonemes;
        bool openSet = false;
    };

    /// One singer, with the languages it declares.
    struct SingerEntry {
        SingerRef ref;
        std::vector<LanguageEntry> languages;

        /// The handle the author names as the default, or empty. A hint for the host; it takes no
        /// part in matching.
        std::string defaultLanguage;

        /// The markers this singer declares for itself (the singer category's reservedPhonemes),
        /// which the session answers instead of sending to a language. Empty when the singer
        /// declares none, in which case the session wide set applies.
        std::vector<std::string> reservedMarkers;
    };

    /// An immutable view of what the unit held when it was built.
    ///
    /// Safe to hold across a refresh: a later scan publishes a new catalog rather than editing
    /// this one, so a host reading a snapshot never sees it change underneath.
    class WOLF_EXPORT LinguistCatalog {
    public:
        const std::vector<SingerEntry> &singers() const noexcept {
            return m_singers;
        }

        /// Finds a singer. An empty version in \a singer matches the only loaded one, and matches
        /// nothing when several are loaded.
        const SingerEntry *find(const SingerRef &singer) const;

        /// The same lookup, telling apart the two ways it can come back empty: \a ambiguous is set
        /// when the singer is there several times over rather than not at all. A caller reporting
        /// to a person needs the difference; the fix is not the same.
        const SingerEntry *find(const SingerRef &singer, bool &ambiguous) const;

    private:
        std::vector<SingerEntry> m_singers;

        friend class LinguistSession;
    };

    /// A handle a host can set from another thread to cut a conversion short.
    ///
    /// Cancelling asks the executives the session has out on loan to stop; it is not a guarantee
    /// that any particular word was or was not converted.
    class WOLF_EXPORT CancelToken {
    public:
        CancelToken();
        ~CancelToken();

        /// Copies share one state, so a token handed to a worker cancels the same conversion the
        /// caller still holds a token for.
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

    /// The lifetime layer over the linguist domain: catalog, readiness, executive pool.
    ///
    /// Every host needs these and none of them is a host decision, which is why they live here
    /// rather than being written once per host. What is a host decision — threading, progress,
    /// the project's own notation — deliberately stays out.
    ///
    /// All members are safe to call from any thread.
    class WOLF_EXPORT LinguistSession {
    public:
        /// Borrows the unit rather than owning it: a host usually has other categories and
        /// services on the same unit. The unit must outlive the session.
        explicit LinguistSession(srt::SynthUnit &unit);

        /// \warning Every convert() must have returned before this runs. The session owns the
        ///          executives a conversion is using, and it has no way to wait for work it did
        ///          not start — the same rule SynthUnit states for its own handles.
        ~LinguistSession();

        /// Rebuilds the catalog from the packages the unit has committed, and publishes it.
        ///
        /// Holders of the old catalog keep seeing the old values. Readiness and failure caches
        /// are dropped with it, and the pool moves to a new generation: idle entries go at once,
        /// entries out on loan go when they come back, so a conversion running on another thread
        /// is never destroyed under it.
        ///
        /// Returns nothing: a package that loaded has already passed validation, so a rescan of
        /// committed state has no failure path.
        void refresh();

        /// Drops one singer's pool entries and package handle, leaving the rest alone. A host
        /// calls this before unloading that voicebank.
        ///
        /// Its languages read Cold afterwards, because a conversion would now load again. A cached
        /// failure survives: releasing frees resources, and it does not make a broken route work.
        /// Entries out on loan are dropped when they come back, as with refresh().
        void release(const SingerRef &singer);

        std::shared_ptr<const LinguistCatalog> catalog() const;

        /// Asks without loading anything and without creating an executive.
        LanguageStatus probe(const SingerRef &singer, std::string_view language) const;

        /// Builds the executive now and keeps it, so the next conversion loads nothing.
        ///
        /// A failure is cached with its reason until the next refresh(): one failure on a five
        /// hundred note phrase must not become five hundred.
        srt::Expected<void> warm(const SingerRef &singer, std::string_view language);

        /// One conversion. Depth, per-word pinning and per-word output keep the L4 shapes.
        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            convert(const SingerRef &singer, std::string_view language,
                    const Api::Linguist::L1::LinguistConvertInput &input,
                    const CancelToken &token = CancelToken());

        /// Tells the session which phonemes a singer can actually sing, so that probe() can
        /// report coverage.
        ///
        /// Supplied by the host because wolf does not read voicebank formats. Without it coverage
        /// reads Unknown, which is deliberately not the same answer as zero: "nobody told me" and
        /// "it covers nothing" call for different reactions.
        ///
        /// The session applies no threshold of its own — a ratio that is good enough is a product
        /// judgement, and the shipped languages range from full coverage to none. The one
        /// exception is total: a language whose inventory is declared complete and which the
        /// singer covers not at all is reported Unavailable, because that is not a degraded route
        /// but a wrong one.
        void setSingerPhonemes(const SingerRef &singer, std::vector<std::string> phonemes);

        /// \name Reserved markers
        ///
        /// The markers the session answers itself rather than sending to a language, for any
        /// singer that declares no set of its own. A singer's own set is its declaration's
        /// reservedPhonemes, a field of the singer category that synthrt reads and the singer's
        /// validator checks against the models, so the session takes it from the catalog and
        /// nobody has to hand it over. This session wide set is the ecosystem convention for the
        /// rest, so a host writing another notation can replace it. Defaults to SP and AP.
        ///
        /// A word the host pinned anything on — a pronunciation or a phoneme layer — is never
        /// compared against this set: pinning is the explicit intent, and the session must not
        /// overwrite it with the marker's own shape.
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
