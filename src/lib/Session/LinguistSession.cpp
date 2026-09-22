#include "LinguistSession.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/SingerContrib.h>

#include "LinguistContrib.h"

namespace LinguistApi = wolf::Api::Linguist::L1;

namespace wolf {

    namespace {

        constexpr char SINGER_CATEGORY[] = "singer";

        /// Orders the pool and the caches. A locator carries no version, so the version is part of
        /// the key rather than something resolved at every lookup.
        struct SingerKey {
            std::string packageId;
            std::string contributionId;
            std::string version;

            bool operator<(const SingerKey &other) const {
                return std::tie(packageId, contributionId, version) <
                       std::tie(other.packageId, other.contributionId, other.version);
            }
        };

        SingerKey keyOf(const SingerEntry &entry) {
            return {entry.ref.locator.packageId(), entry.ref.locator.contributionId(),
                    entry.ref.version.toString()};
        }

    }

    const SingerEntry *LinguistCatalog::find(const SingerRef &singer) const {
        bool ambiguous = false;
        return find(singer, ambiguous);
    }

    const SingerEntry *LinguistCatalog::find(const SingerRef &singer, bool &ambiguous) const {
        ambiguous = false;
        const SingerEntry *found = nullptr;
        for (const auto &entry : m_singers) {
            if (entry.ref.locator.packageId() != singer.locator.packageId() ||
                entry.ref.locator.contributionId() != singer.locator.contributionId()) {
                continue;
            }
            if (!singer.version.isEmpty()) {
                if (entry.ref.version == singer.version) {
                    return &entry;
                }
                continue;
            }
            // An empty version means the only loaded one. A second match makes the request
            // ambiguous, and answering it anyway would route to a version nobody named.
            if (found != nullptr) {
                ambiguous = true;
                return nullptr;
            }
            found = &entry;
        }
        return found;
    }

    class CancelToken::Impl {
    public:
        std::mutex mutex;
        bool cancelled = false;
        std::set<LinguistApi::LinguistExecutive *> running;

        /// Enrols one executive for the duration of a conversion.
        ///
        /// Returns false when the token was already cancelled, which means the conversion must not
        /// start at all. Saying so here rather than calling stop() and starting anyway is what
        /// makes a cancellation that arrives before the batch actually count: the executive would
        /// otherwise consume the request and run the whole chain regardless.
        bool enrol(LinguistApi::LinguistExecutive *executive) {
            std::lock_guard<std::mutex> guard(mutex);
            if (cancelled) {
                return false;
            }
            running.insert(executive);
            return true;
        }

        void retire(LinguistApi::LinguistExecutive *executive) {
            std::lock_guard<std::mutex> guard(mutex);
            running.erase(executive);
        }
    };

    CancelToken::CancelToken() : m_impl(std::make_shared<Impl>()) {
    }

    CancelToken::~CancelToken() = default;
    CancelToken::CancelToken(const CancelToken &other) = default;
    CancelToken &CancelToken::operator=(const CancelToken &other) = default;
    CancelToken::CancelToken(CancelToken &&other) noexcept = default;
    CancelToken &CancelToken::operator=(CancelToken &&other) noexcept = default;

    void CancelToken::cancel() noexcept {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->cancelled = true;
        for (auto executive : m_impl->running) {
            executive->stop();
        }
    }

    bool CancelToken::cancelled() const noexcept {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->cancelled;
    }

    namespace {

        /// One singer's runtime footprint, in the order the framework requires it be torn down.
        ///
        /// The package handle is held because the pipeline was built from a spec that lives inside
        /// the loaded package, and an executive must be destroyed before its package is released.
        /// Holding is not managing: release() and refresh() are the host's release points.
        struct Slot {
            srt::PackageHandle package;
            std::unique_ptr<srt::SingerPipelineExecutive> pipeline;
            std::map<std::string, std::vector<LinguistApi::LinguistExecutive *>> idle;

            /// Conversions currently holding an executive out of this slot. A retired slot with
            /// any of these outstanding stays alive until they come back.
            int outstanding = 0;

            ~Slot() {
                // A child executive is owned by its pipeline and deleting it detaches it, which
                // is the only eviction path there is: createLinguist memoizes nothing, so an
                // undeleted child accumulates under the pipeline forever.
                for (auto &[language, executives] : idle) {
                    for (auto executive : executives) {
                        delete executive;
                    }
                }
                idle.clear();
                pipeline.reset();
            }
        };

        using SlotPtr = std::shared_ptr<Slot>;

    }

    class LinguistSession::Impl {
    public:
        explicit Impl(srt::SynthUnit &unit) : unit(unit) {
        }

        srt::SynthUnit &unit;

        mutable std::mutex mutex;
        std::shared_ptr<const LinguistCatalog> catalog = std::make_shared<LinguistCatalog>();

        std::map<SingerKey, SlotPtr> slots;

        /// Slots a refresh replaced while conversions were still using them. Each stays until its
        /// last lease comes back; nothing new is ever taken from one.
        std::vector<SlotPtr> retiring;

        /// Both caches, dropped together on refresh. Success is cached because it is the pool;
        /// failure because a phrase of five hundred notes would otherwise retry five hundred times.
        std::set<std::pair<SingerKey, std::string>> warmed;
        std::map<std::pair<SingerKey, std::string>, std::string> failed;

        std::vector<std::string> reserved = {"SP", "AP"};

        /// What each singer can sing, as the host told us. Absent means we were not told, which
        /// probe() reports as Unknown rather than as zero.
        std::map<SingerKey, std::set<std::string>> singerPhonemes;

        /// How many missing phonemes probe() hands back. A host showing them to a person needs
        /// examples, not an inventory.
        static constexpr std::size_t MISSING_SHOWN = 16;

        /// Fills in the coverage part of a status. Caller holds the lock.
        void describeCoverage(const SingerKey &key, const LanguageEntry &language,
                              LanguageStatus &status) const {
            status.maxDepth = language.maxDepth;
            if (language.phonemes.empty()) {
                return; // nothing declared, so nothing to measure against
            }
            const auto table = singerPhonemes.find(key);
            if (table == singerPhonemes.end()) {
                return; // not told
            }
            for (const auto &phoneme : language.phonemes) {
                if (table->second.count(phoneme) != 0) {
                    continue;
                }
                if (status.missingPhonemes.size() < MISSING_SHOWN) {
                    status.missingPhonemes.push_back(phoneme);
                }
                status.coverage += 1.0; // counted as missing for now, inverted below
            }
            const auto declared = static_cast<double>(language.phonemes.size());
            const auto missing = status.coverage;
            status.coverage = (declared - missing) / declared;
            status.coverageKind = language.openSet ? LanguageStatus::CoverageKind::Lower
                                                   : LanguageStatus::CoverageKind::Exact;
        }

        /// Builds a catalog from what the unit has committed. Never fails: a committed package has
        /// already passed load validation.
        std::shared_ptr<const LinguistCatalog> scan() const {
            auto built = std::make_shared<LinguistCatalog>();
            for (const auto &package : unit.loadedPackages()) {
                for (auto spec : package.contributions(SINGER_CATEGORY)) {
                    // Everything in the singer category is a SingerSpec, and as<>() is an
                    // unchecked cast, so there is nothing to test here.
                    auto singer = spec->as<srt::SingerSpec>();
                    auto base =
                        srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
                            *singer);
                    auto extension =
                        base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
                    if (extension == nullptr) {
                        // A singer with no linguist contributions is not a linguist domain object,
                        // so it is absent from the catalog rather than present and empty.
                        continue;
                    }

                    SingerEntry entry;
                    // Spelled out with the package id rather than taken from the spec: a spec's
                    // own locator is local to its package, and a host has to be able to hand a
                    // catalog entry straight back.
                    entry.ref.locator = srt::ContribLocator(package.id(), SINGER_CATEGORY,
                                                            spec->locator().contributionId());
                    entry.ref.version = package.version();
                    entry.defaultLanguage = extension->defaultLanguage();
                    entry.reservedMarkers = singer->reservedPhonemes();
                    for (const auto &handle : extension->languages()) {
                        LanguageEntry language;
                        language.handle = handle;
                        if (auto locator = extension->locate(handle)) {
                            language.linguist = *locator;
                        }
                        if (auto binding = extension->binding(handle)) {
                            language.binding = *binding;
                        }
                        language.maxDepth = extension->maxDepth(handle);
                        if (auto values = extension->exports(handle)) {
                            language.phonemes = values->phonemes;
                            language.openSet = values->openSet;
                        }
                        entry.languages.push_back(std::move(language));
                    }
                    built->m_singers.push_back(std::move(entry));
                }
            }
            return built;
        }

        /// Finds or builds the current slot for a singer. Caller holds the lock.
        srt::Expected<SlotPtr> slotFor(const SingerEntry &entry) {
            const auto key = keyOf(entry);
            const auto it = slots.find(key);
            if (it != slots.end()) {
                return it->second;
            }

            auto package = unit.findLoadedPackage(entry.ref.locator.packageId(), entry.ref.version);
            if (!package) {
                return srt::Error(srt::Error::InvalidArgument, "the package holding " +
                                                                   entry.ref.locator.toString() +
                                                                   " is no longer loaded");
            }
            auto spec = package->contribution(SINGER_CATEGORY, entry.ref.locator.contributionId());
            auto singer = spec ? spec->as<srt::SingerSpec>() : nullptr;
            if (singer == nullptr) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "no singer contribution at " + entry.ref.locator.toString());
            }
            auto base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
                *singer);
            auto extension = base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
            if (extension == nullptr) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "no linguist pipeline on " + entry.ref.locator.toString());
            }

            LinguistApi::WolfPipelineRuntimeOptions options;
            auto pipeline = extension->createPipeline(options);
            if (!pipeline) {
                return pipeline.takeError();
            }

            auto slot = std::make_shared<Slot>();
            slot->package = *package;
            slot->pipeline = pipeline.take();
            slots.emplace(key, slot);
            return slot;
        }

        /// Takes an idle executive out of a slot, or nothing when there is none.
        ///
        /// Caller holds the lock. Reserving the lease before returning is what keeps a concurrent
        /// refresh from destroying the slot while the caller is using it.
        LinguistApi::LinguistExecutive *takeIdle(const SlotPtr &slot, std::string_view language) {
            auto &pooled = slot->idle[std::string(language)];
            if (pooled.empty()) {
                ++slot->outstanding;
                return nullptr;
            }
            auto executive = pooled.back();
            pooled.pop_back();
            ++slot->outstanding;
            return executive;
        }

        /// Builds one. \b Caller must NOT hold the lock: this loads dictionaries and opens models,
        /// and holding the session lock across it would make every probe() on any singer wait for
        /// a model to open — the opposite of what a pool is for. The lease is already reserved, so
        /// the slot cannot go away underneath.
        srt::Expected<LinguistApi::LinguistExecutive *> build(const SlotPtr &slot,
                                                              std::string_view language) {
            LinguistApi::LinguistRuntimeOptions options;
            auto created = slot->pipeline->as<LinguistApi::WolfPipelineExecutive>()->createLinguist(
                language, options);
            if (!created) {
                return created.takeError();
            }
            return *created;
        }

        /// Records a failure and hands the lease back. Caller must NOT hold the lock.
        void recordFailure(const SlotPtr &slot, const std::string &language,
                           const std::pair<SingerKey, std::string> &cacheKey,
                           const std::string &reason) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                failed.emplace(cacheKey, reason);
            }
            giveBack(slot, language, nullptr, false);
        }

        /// Gives one back. A slot a refresh has retired takes nothing back; the executive is
        /// deleted instead, and the slot itself goes once the last one has returned.
        void giveBack(const SlotPtr &slot, const std::string &language,
                      LinguistApi::LinguistExecutive *executive, bool reusable) {
            LinguistApi::LinguistExecutive *doomed = nullptr;
            SlotPtr released;
            {
                std::lock_guard<std::mutex> guard(mutex);
                --slot->outstanding;
                const bool current = std::any_of(slots.begin(), slots.end(), [&](const auto &pair) {
                    return pair.second == slot;
                });
                if (executive != nullptr) {
                    if (current && reusable) {
                        slot->idle[language].push_back(executive);
                    } else {
                        // Owned by the pipeline; deleting is what detaches it. Children
                        // accumulate under the pipeline forever otherwise, since createLinguist
                        // memoizes nothing.
                        doomed = executive;
                    }
                }
                if (!current && slot->outstanding == 0) {
                    const auto it = std::find(retiring.begin(), retiring.end(), slot);
                    if (it != retiring.end()) {
                        released = std::move(*it);
                        retiring.erase(it);
                    }
                }
            }
            // Destroyed outside the lock: an executive owns model sessions and dictionaries, and
            // a retired slot owns a pipeline, and neither should hold every probe() on every
            // other singer while it goes.
            delete doomed;
            released.reset();
        }

        /// One executive on loan, with the slot it came out of.
        struct Lease {
            SlotPtr slot;
            LinguistApi::LinguistExecutive *executive = nullptr;
        };

        /// The single path to an executive, shared by warm() and convert().
        ///
        /// Split in two on purpose: everything cheap happens under the lock, and the one expensive
        /// step — building an executive, which loads dictionaries and opens models — happens
        /// outside it. Holding the session lock across that would make a probe() on an unrelated
        /// singer wait for a model to open, which is the opposite of what a pool is for.
        srt::Expected<Lease> acquire(const SingerRef &singer, std::string_view language) {
            SlotPtr slot;
            std::pair<SingerKey, std::string> cacheKey;
            {
                std::lock_guard<std::mutex> guard(mutex);
                bool ambiguous = false;
                auto entry = catalog->find(singer, ambiguous);
                if (entry == nullptr) {
                    return srt::Error(srt::Error::InvalidArgument,
                                      ambiguous
                                          ? "several versions of this singer are loaded; name one"
                                          : "no such singer");
                }
                const auto declared =
                    std::any_of(entry->languages.begin(), entry->languages.end(),
                                [&](const LanguageEntry &item) { return item.handle == language; });
                if (!declared) {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "this singer does not declare " + std::string(language));
                }

                cacheKey = std::make_pair(keyOf(*entry), std::string(language));
                if (const auto failure = failed.find(cacheKey); failure != failed.end()) {
                    return srt::Error(srt::Error::InvalidArgument, failure->second);
                }

                auto found = slotFor(*entry);
                if (!found) {
                    failed.emplace(cacheKey, found.error().message());
                    return found.takeError();
                }
                slot = *found;

                if (auto pooled = takeIdle(slot, language)) {
                    warmed.insert(cacheKey);
                    return Lease{slot, pooled};
                }
                // takeIdle reserved the lease even though it had nothing to give, so the slot
                // survives the build below even if a refresh lands in the middle of it.
            }

            auto built = build(slot, language);
            if (!built) {
                recordFailure(slot, std::string(language), cacheKey, built.error().message());
                return built.takeError();
            }
            {
                std::lock_guard<std::mutex> guard(mutex);
                // Only if the slot is still the current one. A refresh that landed while this was
                // building means the executive is already destined for the bin, and calling that
                // Ready would promise a host that the next conversion loads nothing.
                if (std::any_of(slots.begin(), slots.end(),
                                [&](const auto &pair) { return pair.second == slot; })) {
                    warmed.insert(cacheKey);
                }
            }
            return Lease{slot, *built};
        }

        /// The marker set that applies to one singer. Caller holds the lock.
        std::vector<std::string> reservedFor(const SingerRef &singer) const {
            bool ambiguous = false;
            if (const auto entry = catalog->find(singer, ambiguous)) {
                if (!entry->reservedMarkers.empty()) {
                    return entry->reservedMarkers;
                }
            }
            return reserved;
        }

        static bool isReserved(const std::vector<std::string> &markers, const std::string &lyric) {
            return std::find(markers.begin(), markers.end(), lyric) != markers.end();
        }
    };

    LinguistSession::LinguistSession(srt::SynthUnit &unit) : m_impl(new Impl(unit)) {
        refresh();
    }

    LinguistSession::~LinguistSession() = default;

    void LinguistSession::refresh() {
        auto built = m_impl->scan();
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->catalog = built;
        m_impl->warmed.clear();
        m_impl->failed.clear();
        // Idle entries go now; entries out on loan go when they come back. Emptying the pool
        // outright would destroy an executive a conversion is running on, and rescanning while
        // editing is exactly what a host does.
        for (auto &[key, slot] : m_impl->slots) {
            if (slot->outstanding == 0) {
                continue;
            }
            m_impl->retiring.push_back(slot);
        }
        m_impl->slots.clear();
    }

    void LinguistSession::release(const SingerRef &singer) {
        // One predicate for both loops. Written once because it has to be the same test: an
        // earlier version dropped the version from the readiness sweep, so releasing one version
        // of a voicebank quietly said the other one had gone cold too.
        const auto names = [&](const SingerKey &key) {
            return key.packageId == singer.locator.packageId() &&
                   key.contributionId == singer.locator.contributionId() &&
                   (singer.version.isEmpty() || key.version == singer.version.toString());
        };

        std::lock_guard<std::mutex> guard(m_impl->mutex);
        for (auto it = m_impl->slots.begin(); it != m_impl->slots.end();) {
            if (!names(it->first)) {
                ++it;
                continue;
            }
            if (it->second->outstanding != 0) {
                m_impl->retiring.push_back(it->second);
            }
            it = m_impl->slots.erase(it);
        }
        // The executives are gone, so the next conversion loads again: Ready would be a lie. A
        // cached failure is left alone — freeing resources does not make a broken route work, and
        // refresh() is the only thing that says to look again.
        for (auto it = m_impl->warmed.begin(); it != m_impl->warmed.end();) {
            it = names(it->first) ? m_impl->warmed.erase(it) : std::next(it);
        }
    }

    std::shared_ptr<const LinguistCatalog> LinguistSession::catalog() const {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->catalog;
    }

    LanguageStatus LinguistSession::probe(const SingerRef &singer,
                                          std::string_view language) const {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        bool ambiguous = false;
        auto entry = m_impl->catalog->find(singer, ambiguous);
        if (entry == nullptr) {
            return {Readiness::Unavailable,
                    ambiguous ? "several versions of this singer are loaded; name one"
                              : "no such singer"};
        }
        const auto found =
            std::find_if(entry->languages.begin(), entry->languages.end(),
                         [&](const LanguageEntry &item) { return item.handle == language; });
        if (found == entry->languages.end()) {
            return {Readiness::Unavailable,
                    "this singer does not declare " + std::string(language)};
        }

        const auto key = keyOf(*entry);
        const auto cacheKey = std::make_pair(key, std::string(language));

        LanguageStatus status;
        m_impl->describeCoverage(key, *found, status);

        // The reason is repeated verbatim from wherever it failed. The loader has already said
        // what went wrong; rewording it here would only mean looking the answer up twice.
        if (const auto failure = m_impl->failed.find(cacheKey); failure != m_impl->failed.end()) {
            status.readiness = Readiness::Unavailable;
            status.reason = failure->second;
            return status;
        }
        // The one coverage verdict the session makes itself: a complete inventory the singer
        // covers not at all is a wrong route, not a degraded one. Every other ratio is reported
        // and left to the host, because "good enough" is a product judgement and the shipped
        // languages run from full coverage to none.
        if (status.coverageKind == LanguageStatus::CoverageKind::Exact && status.coverage <= 0.0) {
            status.readiness = Readiness::Unavailable;
            status.reason = "this singer sings none of the " +
                            std::to_string(found->phonemes.size()) + " phonemes " +
                            std::string(language) + " declares";
            return status;
        }
        status.readiness = m_impl->warmed.count(cacheKey) != 0 ? Readiness::Ready : Readiness::Cold;
        return status;
    }

    void LinguistSession::setSingerPhonemes(const SingerRef &singer,
                                            std::vector<std::string> phonemes) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        bool ambiguous = false;
        auto entry = m_impl->catalog->find(singer, ambiguous);
        if (entry == nullptr) {
            return;
        }
        m_impl->singerPhonemes[keyOf(*entry)] =
            std::set<std::string>(phonemes.begin(), phonemes.end());
    }

    srt::Expected<void> LinguistSession::warm(const SingerRef &singer, std::string_view language) {
        auto leased = m_impl->acquire(singer, language);
        if (!leased) {
            return leased.takeError();
        }
        // Warming keeps what it built; that is the whole point of asking for it early.
        m_impl->giveBack(leased->slot, std::string(language), leased->executive, true);
        return {};
    }

    srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>>
        LinguistSession::convert(const SingerRef &singer, std::string_view language,
                                 const LinguistApi::LinguistConvertInput &input,
                                 const CancelToken &token) {
        auto leased = m_impl->acquire(singer, language);
        if (!leased) {
            return leased.takeError();
        }
        const auto slot = leased->slot;
        auto executive = leased->executive;

        // Reserved markers are stopped before dispatch, so the session owes their output shape
        // itself: sending SP into an S2P dictionary is exactly what this avoids. The set is the
        // singer's own when the host supplied one.
        std::vector<std::string> markers;
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            markers = m_impl->reservedFor(singer);
        }
        LinguistApi::LinguistConvertInput forwarded;
        forwarded.depth = input.depth;
        std::vector<std::size_t> positions;
        auto result = std::make_unique<LinguistApi::LinguistConvertResult>();
        result->words.resize(input.words.size());

        for (std::size_t index = 0; index < input.words.size(); ++index) {
            const auto &word = input.words[index];
            // Anything the host pinned is the host stating what they want, so it is never compared
            // against the marker set. That covers a pinned phoneme layer as much as a pinned
            // pronunciation: an earlier version only checked the latter, so a marker note whose
            // phonemes the user had edited by hand had that edit replaced by the marker itself.
            if (!word.pronunciation.has_value() && !word.locked.has_value() &&
                Impl::isReserved(markers, word.lyric)) {
                auto &output = result->words[index];
                output.mode = Api::G2P::L1::Mode::Copy;
                output.pronunciation = word.lyric;
                output.candidates = {word.lyric};
                if (input.depth != LinguistApi::Depth::Pronunciation) {
                    output.phonemes = {word.lyric};
                }
                if (input.depth == LinguistApi::Depth::Onsets) {
                    output.onsets = {true};
                }
                continue;
            }
            positions.push_back(index);
            forwarded.words.push_back(word);
        }

        if (positions.empty()) {
            m_impl->giveBack(slot, std::string(language), executive, true);
            return result;
        }

        if (!token.m_impl->enrol(executive)) {
            // Cancelled before this batch began. The executive was never entered, so it goes
            // straight back into the pool; the caller sees the words the session settled itself
            // and tells cancellation apart by asking the token, which is the only thing that
            // distinguishes "cancelled" from "converted to nothing".
            m_impl->giveBack(slot, std::string(language), executive, true);
            return result;
        }
        auto converted = executive->start(forwarded);
        token.m_impl->retire(executive);

        // An executive a cancellation reached is not one to hand out again, whether or not the
        // conversion still managed to finish: what stop() left behind is the executive's business.
        const bool reusable = !token.cancelled();

        if (!converted) {
            m_impl->giveBack(slot, std::string(language), executive, false);
            return converted.takeError();
        }
        // A short batch would scatter every later word onto the wrong slot, so it is refused.
        if ((*converted)->words.size() != positions.size()) {
            m_impl->giveBack(slot, std::string(language), executive, false);
            // Not a reason to keep it: a module that miscounts once will miscount again.
            return srt::Error(srt::Error::InvalidFormat,
                              "the conversion returned a different number of words than it was "
                              "given");
        }
        for (std::size_t slotIndex = 0; slotIndex < positions.size(); ++slotIndex) {
            result->words[positions[slotIndex]] = std::move((*converted)->words[slotIndex]);
        }
        m_impl->giveBack(slot, std::string(language), executive, reusable);
        return result;
    }

    std::vector<std::string> LinguistSession::reservedMarkers() const {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->reserved;
    }

    void LinguistSession::setReservedMarkers(std::vector<std::string> markers) {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->reserved = std::move(markers);
    }

}
