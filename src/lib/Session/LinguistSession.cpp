#include "LinguistSession.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/SingerContrib.h>

#include "LinguistContrib.h"
#include "Logging.h"

namespace LinguistApi = wolf::Api::Linguist::L1;

namespace wolf {

    namespace {

        /// Key of the pool and the caches. A locator carries no version, and the version is
        /// therefore part of the key instead of being resolved at every lookup.
        struct SingerKey {
            std::string packageId;
            std::string contributionId;
            std::string version;

            bool operator<(const SingerKey &other) const {
                return std::tie(packageId, contributionId, version) <
                       std::tie(other.packageId, other.contributionId, other.version);
            }
        };

        SingerKey keyOf(const SingerRef &ref) {
            return {ref.locator.packageId(), ref.locator.contributionId(), ref.version.toString()};
        }

        SingerKey keyOf(const SingerEntry &entry) {
            return keyOf(entry.ref);
        }

        /// Finds the entry that \a singer names in a sequence of entries that each carry a
        /// \c ref.
        ///
        /// The catalog and the list of unmounted singers share this function, because both must
        /// resolve the same reference identically.
        template <class Entries>
        auto findByRef(const Entries &entries, const SingerRef &singer,
                       bool &ambiguous) -> decltype(&*std::begin(entries)) {
            ambiguous = false;
            decltype(&*std::begin(entries)) found = nullptr;
            for (const auto &entry : entries) {
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
                // An empty version selects the only loaded version. A second match makes the
                // request ambiguous, and resolving it would route to a version that the caller did
                // not name.
                if (found != nullptr) {
                    ambiguous = true;
                    return nullptr;
                }
                found = &entry;
            }
            return found;
        }

        /// A singer that declares languages but carries no linguist pipeline, with the reason for
        /// each declared language.
        struct UnmountedSinger {
            SingerRef ref;
            std::map<std::string, std::string> reasons;
        };

        /// Result of one scan of the unit.
        struct ScanResult {
            std::shared_ptr<const LinguistCatalog> catalog;
            std::vector<UnmountedSinger> unmounted;
        };

        /// Returns the reason why a language of a singer without a linguist pipeline was not
        /// mounted.
        ///
        /// The loader calls only the validators of existing providers. A singer loaded in a
        /// transaction that creates no wolf provider therefore loads with an unvalidated language
        /// map and carries no pipeline. The session cannot reject the package, and it reports the
        /// defect of the route instead of reporting that the singer does not exist.
        std::string unmountedReason(const srt::SingerSpec &singer, const std::string &language,
                                    const std::string &role) {
            const auto import = singer.findImport(role);
            if (!import || !import->binding()) {
                return "language " + language + " routes to role " + role +
                       ", which has no prepared binding";
            }
            const auto &target = import->binding()->target();
            if (target.locator().category() != LINGUIST_CATEGORY) {
                return "language " + language + " routes to role " + role + ", whose target " +
                       target.locator().category() + "/" + target.locator().contributionId() +
                       " is not a linguist contribution; no linguist provider was loaded with this "
                       "singer, and its language map was therefore not validated at load";
            }
            return "language " + language + " routes to role " + role +
                   ", but no linguist provider mounted this singer's language map";
        }

    }

    const SingerEntry *LinguistCatalog::find(const SingerRef &singer) const {
        bool ambiguous = false;
        return find(singer, ambiguous);
    }

    const SingerEntry *LinguistCatalog::find(const SingerRef &singer, bool &ambiguous) const {
        return findByRef(m_singers, singer, ambiguous);
    }

    class CancelToken::Impl {
    public:
        std::mutex mutex;
        std::condition_variable stopped;
        bool cancelled = false;
        std::set<LinguistApi::LinguistExecutive *> running;

        /// Executives on which a cancel() is currently calling stop(), one entry per call in
        /// progress. retire() waits until an executive leaves this set, because the session may
        /// destroy an executive as soon as it is retired, and stop() is called outside the lock.
        std::multiset<LinguistApi::LinguistExecutive *> stopping;

        /// Enrols one executive for the duration of a conversion.
        ///
        /// Returns false if the token was already cancelled, in which case the conversion must not
        /// start. Returning false here, instead of calling stop() and starting, ensures that a
        /// cancellation before the batch takes effect. Otherwise the executive would consume the
        /// request and run the whole chain.
        bool enrol(LinguistApi::LinguistExecutive *executive) {
            std::lock_guard<std::mutex> guard(mutex);
            if (cancelled) {
                return false;
            }
            running.insert(executive);
            return true;
        }

        /// Withdraws one executive and waits while a cancel() is still stopping it. Only the
        /// conversion that holds that executive waits. enrol(), cancelled() and retire() for every
        /// other executive proceed.
        void retire(LinguistApi::LinguistExecutive *executive) {
            std::unique_lock<std::mutex> lock(mutex);
            running.erase(executive);
            stopped.wait(lock, [&] { return stopping.count(executive) == 0; });
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
        // stop() runs outside the lock. Otherwise a variant with a slow stop() would block every
        // enrol() and retire() on this token, and with them every conversion that shares the
        // token. The snapshot is registered in stopping first, so that no executive in it can be
        // retired, and therefore destroyed, while stop() is still running on it.
        std::vector<LinguistApi::LinguistExecutive *> snapshot;
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            m_impl->cancelled = true;
            snapshot.assign(m_impl->running.begin(), m_impl->running.end());
            m_impl->stopping.insert(snapshot.begin(), snapshot.end());
        }
        for (auto executive : snapshot) {
            (void) executive->stop();
        }
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            for (auto executive : snapshot) {
                m_impl->stopping.erase(m_impl->stopping.find(executive));
            }
        }
        m_impl->stopped.notify_all();
    }

    bool CancelToken::cancelled() const noexcept {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->cancelled;
    }

    namespace {

        /// Runtime resources of one singer, declared in the teardown order that the framework
        /// requires.
        ///
        /// The package handle is held because the pipeline was created from a spec inside the
        /// loaded package, and an executive must be destroyed before its package is released.
        /// Holding the handle does not imply lifetime management: release() and refresh() are the
        /// release points of the host.
        struct Slot {
            srt::PackageHandle package;
            std::unique_ptr<srt::SingerPipelineExecutive> pipeline;
            std::map<std::string, std::vector<std::unique_ptr<LinguistApi::LinguistExecutive>>>
                idle;

            /// Number of conversions that currently hold an executive from this slot. A retired
            /// slot with outstanding conversions remains alive until they return.
            int outstanding = 0;

            ~Slot() {
                // Every executive comes from createOwnedLinguist(), and releasing one detaches it
                // from the pipeline. The executives are destroyed first, while their pipeline still
                // exists. Otherwise the pipeline would destroy them and the pointers here would
                // destroy them a second time.
                idle.clear();
                pipeline.reset();
            }
        };

        using SlotPtr = std::shared_ptr<Slot>;

        /// Renders a cached failure for a host.
        ///
        /// The cache keeps the error object rather than its text, because the code and the cause
        /// chain identify the layer that failed. A host receives no access to that object, so the
        /// replay keeps what the code and the chain carry: the kind of the code, and the message of
        /// every level of the chain. The kind is left out when the message already is the canned
        /// text of the code, so that a failure recorded without text does not read as
        /// "file not found: file not found".
        std::string describeFailure(const srt::Error &error) {
            std::string reason;
            const auto kind = error.code().message();
            if (!kind.empty() && kind != error.message()) {
                reason = kind + ": ";
            }
            reason += error.toString();
            return reason;
        }

    }

    class LinguistSession::Impl {
    public:
        explicit Impl(srt::SynthUnit &unit) : unit(unit) {
        }

        srt::SynthUnit &unit;

        mutable std::mutex mutex;
        std::shared_ptr<const LinguistCatalog> catalog = std::make_shared<LinguistCatalog>();

        /// Singers that the last scan excluded from the catalog although they declare languages.
        std::vector<UnmountedSinger> unmounted;

        std::map<SingerKey, SlotPtr> slots;

        /// Slots that a refresh replaced while conversions were still using them. Each slot
        /// remains until its last lease returns, and no new lease is taken from it.
        std::vector<SlotPtr> retiring;

        /// Both caches, discarded together on refresh. Success is cached because it represents the
        /// pool. Failure is cached because a phrase of five hundred notes would otherwise retry
        /// five hundred times.
        ///
        /// A failure is stored as the error object instead of its text. The plugins distinguish a
        /// missing resource from an unreadable resource and an unsupported feature from a
        /// malformed declaration by the error code, and the cause chain identifies the failing
        /// layer. A replay of the text alone would return a different fault to every later caller
        /// than the fault that the first caller received.
        std::set<std::pair<SingerKey, std::string>> warmed;
        std::map<std::pair<SingerKey, std::string>, srt::Error> failed;

        std::vector<std::string> reserved = {"SP", "AP"};

        /// Phonemes that each singer can sing, as supplied by the host. An absent entry indicates
        /// that no table was supplied, which probe() reports as Unknown instead of zero.
        std::map<SingerKey, std::set<std::string>> singerPhonemes;

        /// Maximum number of missing phonemes that probe() returns. The list serves as examples
        /// for display, not as an inventory.
        static constexpr std::size_t MISSING_SHOWN = 16;

        /// Fills in the coverage part of a status. Caller holds the lock.
        void describeCoverage(const SingerKey &key, const LanguageEntry &language,
                              LanguageStatus &status) const {
            status.maxDepth = language.maxDepth;
            if (language.phonemes.empty()) {
                return; // no declared inventory to measure against
            }
            const auto table = singerPhonemes.find(key);
            if (table == singerPhonemes.end()) {
                return; // no table supplied
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

        /// Builds a catalog from the packages committed in the unit. The function cannot fail,
        /// because a committed package has already passed load validation.
        ///
        /// A singer that declares languages and carries no linguist pipeline is excluded from the
        /// catalog, like every singer without linguist contributions, and recorded with the
        /// reason so that probe(), warm() and convert() can report it.
        ScanResult scan() const {
            auto built = std::make_shared<LinguistCatalog>();
            std::vector<UnmountedSinger> unmounted;
            for (const auto &package : unit.loadedPackages()) {
                for (auto spec : package.contributions(srt::SingerCategory::NAME)) {
                    // Every contribution of the singer category is a SingerSpec, and as<>() is an
                    // unchecked cast, so no check is required.
                    auto singer = spec->as<srt::SingerSpec>();
                    auto extension = LinguistApi::WolfPipelineExtension::from(*singer);
                    if (extension == nullptr) {
                        // A singer without linguist contributions is not a linguist domain object
                        // and is therefore absent from the catalog instead of present and empty.
                        if (!singer->languages().empty()) {
                            UnmountedSinger record;
                            record.ref.locator =
                                srt::ContribLocator(package.id(), srt::SingerCategory::NAME,
                                                    spec->locator().contributionId());
                            record.ref.version = package.version();
                            for (const auto &[language, role] : singer->languages()) {
                                record.reasons.emplace(language,
                                                       unmountedReason(*singer, language, role));
                            }
                            logCategory().srtWarning(
                                "singer %1 declares languages but carries no linguist pipeline: %2",
                                record.ref.locator.toString(), record.reasons.begin()->second);
                            unmounted.push_back(std::move(record));
                        }
                        continue;
                    }

                    SingerEntry entry;
                    // Constructed with the package id instead of taken from the spec, because the
                    // locator of a spec is local to its package and a host must be able to pass a
                    // catalog entry back unchanged.
                    entry.ref.locator = srt::ContribLocator(package.id(), srt::SingerCategory::NAME,
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
            return {built, std::move(unmounted)};
        }

        /// Returns the recorded reason why a singer is absent from the catalog. The caller holds
        /// the lock.
        ///
        /// Returns an empty string if the singer is not loaded. Otherwise returns the reason
        /// recorded for \a language, or a statement that the singer does not declare it.
        std::string unmountedReasonFor(const SingerRef &singer, std::string_view language) const {
            bool ambiguous = false;
            const auto record = findByRef(unmounted, singer, ambiguous);
            if (record == nullptr) {
                return {};
            }
            const auto it = record->reasons.find(std::string(language));
            if (it == record->reasons.end()) {
                return "this singer does not declare " + std::string(language);
            }
            return it->second;
        }

        /// Finds or creates the current slot for a singer. The caller holds the lock.
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
            auto spec = package->contribution(srt::SingerCategory::NAME,
                                              entry.ref.locator.contributionId());
            auto singer = spec ? spec->as<srt::SingerSpec>() : nullptr;
            if (singer == nullptr) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "no singer contribution at " + entry.ref.locator.toString());
            }
            auto extension = LinguistApi::WolfPipelineExtension::from(*singer);
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

        /// Takes an idle executive from a slot. Returns nullptr if the slot has no idle executive.
        ///
        /// The caller holds the lock. The lease is reserved before the function returns, which
        /// prevents a concurrent refresh from destroying the slot while the caller uses it.
        std::unique_ptr<LinguistApi::LinguistExecutive> takeIdle(const SlotPtr &slot,
                                                                 std::string_view language) {
            auto &pooled = slot->idle[std::string(language)];
            if (pooled.empty()) {
                ++slot->outstanding;
                return nullptr;
            }
            auto executive = std::move(pooled.back());
            pooled.pop_back();
            ++slot->outstanding;
            return executive;
        }

        /// Creates one executive. The caller \b must \b not hold the lock, because this function
        /// loads dictionaries and opens models, and holding the session lock would make every
        /// probe() on any singer wait for a model to open, which defeats the purpose of the pool.
        /// The lease is already reserved, so the slot cannot be destroyed during the call.
        srt::Expected<std::unique_ptr<LinguistApi::LinguistExecutive>>
            build(const SlotPtr &slot, std::string_view language) {
            LinguistApi::LinguistRuntimeOptions options;
            return slot->pipeline->as<LinguistApi::WolfPipelineExecutive>()->createOwnedLinguist(
                language, options);
        }

        /// Records a failure and returns the lease. The caller \b must \b not hold the lock.
        ///
        /// \a executive is passed on to giveBack() rather than destroyed by the caller: an
        /// executive must be released before the slot that owns its pipeline, and only a caller
        /// that holds one has to hand it in.
        void recordFailure(const SlotPtr &slot, const std::string &language,
                           const std::pair<SingerKey, std::string> &cacheKey,
                           const srt::Error &error,
                           std::unique_ptr<LinguistApi::LinguistExecutive> executive = nullptr) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                failed.emplace(cacheKey, error);
            }
            giveBack(slot, language, std::move(executive), false);
        }

        /// Returns an executive to its slot. A slot retired by a refresh accepts no executive. The
        /// executive is deleted instead, and the slot is destroyed after the last executive has
        /// returned.
        void giveBack(const SlotPtr &slot, const std::string &language,
                      std::unique_ptr<LinguistApi::LinguistExecutive> executive, bool reusable) {
            std::unique_ptr<LinguistApi::LinguistExecutive> doomed;
            SlotPtr released;
            {
                std::lock_guard<std::mutex> guard(mutex);
                --slot->outstanding;
                const bool current = std::any_of(slots.begin(), slots.end(), [&](const auto &pair) {
                    return pair.second == slot;
                });
                if (executive != nullptr) {
                    if (current && reusable) {
                        slot->idle[language].push_back(std::move(executive));
                    } else {
                        // Releasing the pointer detaches the executive from the pipeline. The
                        // pointer is therefore released instead of kept, outside the lock below.
                        doomed = std::move(executive);
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
            // Destroyed outside the lock, because an executive owns model sessions and
            // dictionaries and a retired slot owns a pipeline, and neither destruction may block
            // probe() on other singers.
            doomed.reset();
            released.reset();
        }

        /// One lent executive, with the slot from which it was taken and the key under which its
        /// result is recorded.
        ///
        /// The key travels with the lease because a failure found during the conversion must be
        /// cached exactly as one found while acquiring the lease. Recomputing the key at that point
        /// would repeat the lookup of acquire() and could resolve another entry after a refresh.
        struct Lease {
            SlotPtr slot;
            std::unique_ptr<LinguistApi::LinguistExecutive> executive;
            std::pair<SingerKey, std::string> cacheKey;
        };

        /// Acquires an executive. warm() and convert() share this single acquisition path.
        ///
        /// The function is intentionally split into two phases. All inexpensive steps run under
        /// the lock, and the only expensive step, creating an executive, which loads dictionaries
        /// and opens models, runs outside it. Holding the session lock during that step would make
        /// a probe() on an unrelated singer wait for a model to open, which defeats the purpose of
        /// the pool.
        srt::Expected<Lease> acquire(const SingerRef &singer, std::string_view language) {
            SlotPtr slot;
            std::pair<SingerKey, std::string> cacheKey;
            {
                std::lock_guard<std::mutex> guard(mutex);
                bool ambiguous = false;
                auto entry = catalog->find(singer, ambiguous);
                if (entry == nullptr) {
                    if (ambiguous) {
                        return srt::Error(srt::Error::InvalidArgument,
                                          "several versions of this singer are loaded; name one");
                    }
                    const auto reason = unmountedReasonFor(singer, language);
                    return srt::Error(srt::Error::InvalidArgument,
                                      reason.empty() ? "no such singer" : reason);
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
                    return failure->second;
                }

                auto found = slotFor(*entry);
                if (!found) {
                    failed.emplace(cacheKey, found.error());
                    return found.takeError();
                }
                slot = *found;

                if (auto pooled = takeIdle(slot, language)) {
                    warmed.insert(cacheKey);
                    return Lease{slot, std::move(pooled), cacheKey};
                }
                // takeIdle reserved the lease although it returned no executive, so the slot
                // survives the creation below even if a refresh occurs during it.
            }

            auto built = build(slot, language);
            if (!built) {
                recordFailure(slot, std::string(language), cacheKey, built.error());
                return built.takeError();
            }
            {
                std::lock_guard<std::mutex> guard(mutex);
                // Recorded only if the slot is still current. After a refresh during creation the
                // executive is discarded on return, and reporting Ready would incorrectly indicate
                // that the next conversion loads no resources.
                if (std::any_of(slots.begin(), slots.end(),
                                [&](const auto &pair) { return pair.second == slot; })) {
                    warmed.insert(cacheKey);
                }
            }
            return Lease{slot, built.take(), cacheKey};
        }

        /// Returns the marker set that applies to one singer. The caller holds the lock.
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
        auto scanned = m_impl->scan();
        const auto &built = scanned.catalog;
        // Removed under the lock and destroyed after the lock is released, for the reason stated
        // in giveBack(): a slot owns a pipeline, and its idle executives own model sessions and
        // dictionaries. Closing them under the session lock would make every probe() on every
        // singer wait, and a host rescans after every installation.
        std::map<SingerKey, SlotPtr> dropped;
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            m_impl->catalog = built;
            m_impl->unmounted = std::move(scanned.unmounted);
            m_impl->warmed.clear();
            m_impl->failed.clear();
            // A phoneme table survives a refresh only for a singer that the new catalog still
            // holds. Otherwise every voicebank installed during a session would keep its table.
            std::set<SingerKey> present;
            for (const auto &entry : built->singers()) {
                present.insert(keyOf(entry));
            }
            for (auto it = m_impl->singerPhonemes.begin(); it != m_impl->singerPhonemes.end();) {
                it = present.count(it->first) != 0 ? std::next(it)
                                                   : m_impl->singerPhonemes.erase(it);
            }
            // Idle entries are destroyed now, and lent entries are destroyed when they return.
            // Emptying the pool immediately would destroy an executive during a conversion, and
            // hosts rescan during editing.
            for (auto &[key, slot] : m_impl->slots) {
                if (slot->outstanding == 0) {
                    continue;
                }
                m_impl->retiring.push_back(slot);
            }
            dropped.swap(m_impl->slots);
        }
        dropped.clear();
    }

    void LinguistSession::release(const SingerRef &singer) {
        // Both loops share one predicate, because they must apply the same test. An earlier
        // implementation omitted the version from the readiness sweep, and releasing one version
        // of a voicebank then silently reported the other version as Cold.
        const auto names = [&](const SingerKey &key) {
            return key.packageId == singer.locator.packageId() &&
                   key.contributionId == singer.locator.contributionId() &&
                   (singer.version.isEmpty() || key.version == singer.version.toString());
        };

        // Destroyed after the lock is released, as in refresh().
        std::vector<SlotPtr> dropped;
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            for (auto it = m_impl->slots.begin(); it != m_impl->slots.end();) {
                if (!names(it->first)) {
                    ++it;
                    continue;
                }
                if (it->second->outstanding != 0) {
                    m_impl->retiring.push_back(it->second);
                }
                dropped.push_back(std::move(it->second));
                it = m_impl->slots.erase(it);
            }
            // The executives are destroyed, and the next conversion loads the resources again,
            // so Ready would be incorrect. A cached failure is retained, because releasing
            // resources does not repair a failed route, and only refresh() clears the failure
            // cache.
            for (auto it = m_impl->warmed.begin(); it != m_impl->warmed.end();) {
                it = names(it->first) ? m_impl->warmed.erase(it) : std::next(it);
            }
        }
        dropped.clear();
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
            if (ambiguous) {
                return {Readiness::Unavailable,
                        "several versions of this singer are loaded; name one"};
            }
            const auto reason = m_impl->unmountedReasonFor(singer, language);
            return {Readiness::Unavailable, reason.empty() ? "no such singer" : reason};
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

        // The reason comes from the failing layer, which has already described the fault, and it
        // carries the kind of the code and the cause chain as well as the text: the code and the
        // chain identify the layer, and a host has no other way to reach them.
        if (const auto failure = m_impl->failed.find(cacheKey); failure != m_impl->failed.end()) {
            status.readiness = Readiness::Unavailable;
            status.reason = describeFailure(failure->second);
            return status;
        }
        // This is the only coverage decision that the session makes: a complete inventory of
        // which the singer covers no phoneme is an invalid route, not a degraded route. Every
        // other ratio is reported and left to the host, because a sufficient ratio is a product
        // decision and the shipped languages range from full coverage to none.
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
            // The function returns void, because coverage is advisory and a host sets it while
            // scanning its voicebanks, which may precede refresh(). The reason is logged, because
            // a silent failure would leave the cause of an Unknown coverage undiagnosed.
            logCategory().srtWarning(
                "setSingerPhonemes ignored for %1: %2", singer.locator.toString(),
                ambiguous ? "several versions of this singer are loaded; name one"
                          : "no such singer in the current catalog");
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
        // Warming retains the created executive, which is the purpose of warming.
        m_impl->giveBack(leased->slot, std::string(language), std::move(leased->executive), true);
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
        const auto cacheKey = leased->cacheKey;
        auto executive = std::move(leased->executive);

        // Reserved markers are intercepted before dispatch, and the session therefore produces
        // their output itself. This prevents SP from reaching an S2P dictionary. The set is the
        // set of the singer if the singer declares one.
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
            // A pinned value expresses the intent of the host and is never compared against the
            // marker set. This applies to a pinned phoneme layer as well as to a pinned
            // pronunciation. An earlier implementation checked only the pronunciation, and the
            // manually edited phonemes of a marker note were then replaced by the marker.
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
            m_impl->giveBack(slot, std::string(language), std::move(executive), true);
            return result;
        }

        if (!token.m_impl->enrol(executive.get())) {
            // Cancelled before this batch began. The executive was not started, and it returns
            // to the pool directly. The caller receives the words that the session resolved
            // itself and detects the cancellation by querying the token, which is the only way to
            // distinguish a cancelled conversion from a conversion with empty output.
            m_impl->giveBack(slot, std::string(language), std::move(executive), true);
            return result;
        }
        auto converted = executive->start(forwarded);
        token.m_impl->retire(executive.get());

        // An executive reached by a cancellation is not lent again, regardless of whether the
        // conversion finished, because the state after stop() is internal to the executive.
        const bool reusable = !token.cancelled();

        if (!converted) {
            // The stopping rule wins over a failure that raced with it: a cancelled token is not an
            // error (see LinguistSession.h), so the caller receives the words the session resolved
            // itself and no failure is recorded. Recording one would fault the route for a stop
            // that the host asked for, and the next unstopped conversion would report the fault
            // again if it is real.
            if (token.cancelled()) {
                m_impl->giveBack(slot, std::string(language), std::move(executive), false);
                return result;
            }
            // A batch that did not run at all is a property of the route rather than of the words:
            // the executive failed as a whole, so a later conversion is not expected to succeed
            // where this one did not. The failure is cached the way warm() caches one, and probe()
            // reports the route Unavailable until the next refresh(). A per-word failure is a
            // result of the conversion instead, and is never cached.
            auto error = converted.takeError();
            m_impl->recordFailure(slot, std::string(language), cacheKey, error,
                                 std::move(executive));
            return error;
        }
        // A batch with a different word count would assign every later word to the wrong
        // position, and it is therefore rejected. A stopped conversion may legitimately return
        // fewer words, so the count is only a fault when the run was not stopped.
        if ((*converted)->words.size() != positions.size()) {
            if (token.cancelled()) {
                m_impl->giveBack(slot, std::string(language), std::move(executive), false);
                return result;
            }
            // A module that returns a wrong count is a fault of the route rather than of the words,
            // so it is remembered like any other failure of the whole batch. The executive is not
            // reused either, because a module that returned a wrong count once is likely to repeat
            // the error.
            const auto error =
                srt::Error(srt::Error::InvalidFormat,
                           "the conversion returned a different number of words than it was given");
            m_impl->recordFailure(slot, std::string(language), cacheKey, error,
                                 std::move(executive));
            return error;
        }
        for (std::size_t slotIndex = 0; slotIndex < positions.size(); ++slotIndex) {
            result->words[positions[slotIndex]] = std::move((*converted)->words[slotIndex]);
        }
        m_impl->giveBack(slot, std::string(language), std::move(executive), reusable);
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
