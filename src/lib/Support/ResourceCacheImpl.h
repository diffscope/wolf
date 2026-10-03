#ifndef WOLF_RESOURCECACHEIMPL_H
#define WOLF_RESOURCECACHEIMPL_H

// State of the resource cache. It is kept out of ResourceCache.cpp because one other file reads
// it: the resource cache test inspects the index through ResourceCacheProbe, the class that
// ResourceCache declares as a friend for that purpose. Nothing in this header is exported or
// installed, and the probe is compiled into the test, so no test code reaches the symbol table of
// the library.

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Support/Expected.h>

#include <wolf/Support/ResourceCache.h>

namespace wolf {

    namespace resourcecache {

        namespace fs = std::filesystem;

        /// A BLAKE3 hash. The length is checked against the constant of the BLAKE3 library at the
        /// point where the hash is computed, and this header therefore requires no BLAKE3
        /// include.
        using Digest = std::array<std::uint8_t, 32>;

        /// Path memo record that allows an unchanged file to be skipped on a later read.
        struct Stamp {
            std::uintmax_t size = 0;
            fs::file_time_type modified;
            Digest digest{};
        };

    }

    class ResourceCache::Impl {
        using Digest = resourcecache::Digest;
        using Stamp = resourcecache::Stamp;

    public:
        std::shared_mutex mutex;

        /// Content addressed entries. The key combines content, kind and parser generation, so
        /// that entries stop matching after a format change without explicit invalidation.
        std::map<std::tuple<Digest, std::string, std::string>, std::weak_ptr<const void>> entries;

        /// Path memo. An unchanged size and timestamp allow a repeated load to skip the read.
        std::map<std::filesystem::path, Stamp> stamps;

        /// Result of one parse, in a form that a future delivers to every waiter.
        struct Outcome {
            std::shared_ptr<const void> value;
            std::optional<srt::Error> error;
        };

        /// Parses in progress, one per key. A second executive that requests the same resource
        /// waits for the first parse instead of starting its own, and no parse runs under the
        /// lock. A dictionary of a few hundred thousand lines would otherwise hold the exclusive
        /// lock for the duration of the read and serialize every unrelated acquire in the
        /// process.
        std::map<std::tuple<Digest, std::string, std::string>, std::shared_future<Outcome>>
            inflight;

        std::size_t parses = 0;

        /// Insertions since the last sweep.
        std::size_t insertions = 0;

        /// Removes entries whose value has expired and, if the path memo has exceeded the same
        /// bound, clears the memo as well.
        ///
        /// Each expired entry occupies a map node and a control block. Without removal, a host
        /// that loads and unloads packages throughout a session, as a project editor does, would
        /// grow this index without limit. The sweep runs on insertion instead of on a timer, which
        /// requires no dedicated thread and no configurable policy.
        ///
        /// Clearing the memo is always safe. The memo only allows an unchanged file to skip
        /// rehashing, and its loss costs one read and changes no result.
        void sweep() {
            for (auto it = entries.begin(); it != entries.end();) {
                it = it->second.expired() ? entries.erase(it) : std::next(it);
            }
            if (stamps.size() > SWEEP_THRESHOLD) {
                stamps.clear();
            }
            insertions = 0;
        }

        /// Records one insertion and sweeps once every SWEEP_THRESHOLD insertions. The caller
        /// holds the exclusive lock.
        ///
        /// The sweep is triggered by an insertion count instead of by size, because an index that
        /// holds SWEEP_THRESHOLD live entries would otherwise sweep on every subsequent insertion
        /// without removing anything. The count distributes the cost of a sweep over the
        /// insertions that can have produced expired entries.
        void noteInsertion() {
            if (++insertions >= SWEEP_THRESHOLD) {
                sweep();
            }
        }

        /// Maximum number of insertions between two sweeps. The value is large enough that the
        /// common case, a few dozen resources loaded once, never triggers a sweep.
        static constexpr std::size_t SWEEP_THRESHOLD = 256;

        /// Settles one parse in progress: removes it from inflight, publishes its value, and
        /// delivers the outcome to every executive that waits for it.
        ///
        /// If the parse exits through an exception, the destructor settles it with an error.
        /// Otherwise the key would remain in inflight for the lifetime of the process, every later
        /// acquire of the same resource would wait on a future that is never fulfilled, and the
        /// queued waiters would receive std::future_error (broken_promise) instead of an error
        /// message.
        class Settlement {
        public:
            using Key = std::tuple<Digest, std::string, std::string>;

            Settlement(Impl &impl, Key key, std::promise<Outcome> &promise,
                       std::filesystem::path path)
                : m_impl(impl), m_key(std::move(key)), m_promise(promise), m_path(std::move(path)) {
            }

            ~Settlement() {
                if (m_done) {
                    return;
                }
                Outcome failed;
                // A parser is provider code, and a parser that throws violates the contract that
                // requires it to report failures through its result.
                failed.error = srt::Error(srt::Error::InvalidFormat,
                                          stdc::path::to_utf8(m_path) +
                                              ": the parser of this resource raised an exception");
                settle(std::move(failed), {});
            }

            void settle(Outcome outcome,
                        std::optional<std::pair<std::filesystem::path, Stamp>> stamp) {
                {
                    std::unique_lock lock(m_impl.mutex);
                    m_impl.inflight.erase(m_key);
                    if (outcome.value) {
                        m_impl.entries[m_key] = outcome.value;
                        if (stamp) {
                            m_impl.stamps[stamp->first] = stamp->second;
                        }
                        ++m_impl.parses;
                        m_impl.noteInsertion();
                    }
                }
                m_done = true;
                m_promise.set_value(std::move(outcome));
            }

            Settlement(const Settlement &) = delete;
            Settlement &operator=(const Settlement &) = delete;

        private:
            Impl &m_impl;
            Key m_key;
            std::promise<Outcome> &m_promise;
            std::filesystem::path m_path;
            bool m_done = false;
        };
    };

}

#endif // WOLF_RESOURCECACHEIMPL_H
