#include "ResourceCache.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <tuple>
#include <vector>

#include <blake3.h>
#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace {

        using Digest = std::array<std::uint8_t, BLAKE3_OUT_LEN>;

        /// Streams a file through the hash rather than holding it: these resources run to
        /// hundreds of thousands of lines.
        srt::Expected<Digest> hashFile(const fs::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return srt::Error(
                    srt::Error::FileNotOpen,
                    stdc::path::to_utf8(path) + ": failed to open a resource for hashing");
            }
            blake3_hasher hasher;
            blake3_hasher_init(&hasher);
            std::vector<char> buffer(64 * 1024);
            while (file) {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto read = static_cast<std::size_t>(file.gcount());
                if (read > 0) {
                    blake3_hasher_update(&hasher, buffer.data(), read);
                }
            }
            Digest digest{};
            blake3_hasher_finalize(&hasher, digest.data(), digest.size());
            return digest;
        }

        /// What the path memo remembers so an unchanged file need not be read again.
        struct Stamp {
            std::uintmax_t size = 0;
            fs::file_time_type modified;
            Digest digest{};
        };

        srt::Expected<std::pair<std::uintmax_t, fs::file_time_type>>
            statFile(const fs::path &path) {
            std::error_code error;
            const auto size = fs::file_size(path, error);
            if (error) {
                return srt::Error(srt::Error::FileNotFound,
                                  stdc::path::to_utf8(path) + ": failed to size a resource");
            }
            const auto modified = fs::last_write_time(path, error);
            if (error) {
                return srt::Error(srt::Error::FileNotFound,
                                  stdc::path::to_utf8(path) + ": failed to stat a resource");
            }
            return std::make_pair(size, modified);
        }

    }

    class ResourceCache::Impl {
    public:
        std::shared_mutex mutex;

        /// Content addressed entries. The key is content, kind and parser generation together,
        /// which is why a format change stops matching without an explicit invalidation.
        std::map<std::tuple<Digest, std::string, std::string>, std::weak_ptr<const void>> entries;

        /// Path memo: an unchanged size and timestamp let a repeat load skip the read entirely.
        std::map<fs::path, Stamp> stamps;

        /// What one parse produced, in a shape a future can carry to every waiter.
        struct Outcome {
            std::shared_ptr<const void> value;
            std::optional<srt::Error> error;
        };

        /// Parses under way, one per key. A second executive asking for the same resource waits
        /// on the first parse instead of starting its own, and nobody parses under the lock: a
        /// dictionary of a few hundred thousand lines held the exclusive lock for as long as it
        /// took to read, which serialized every unrelated acquire in the process behind it.
        std::map<std::tuple<Digest, std::string, std::string>, std::shared_future<Outcome>>
            inflight;

        std::size_t parses = 0;

        /// Drops entries whose value has died and, when the path memo has grown past the same
        /// bound, the memo with them.
        ///
        /// An expired entry costs a map node and a control block each, and nothing ever removed
        /// them: a host that loads and unloads packages all session long — which is what editing a
        /// project looks like — grew this index without limit. Swept on insertion rather than on a
        /// timer, so there is no thread to own and no policy to configure.
        ///
        /// Dropping the memo outright is always safe: it only lets an unchanged file skip being
        /// re-hashed, so losing it costs one read and changes no result.
        void sweep() {
            for (auto it = entries.begin(); it != entries.end();) {
                it = it->second.expired() ? entries.erase(it) : std::next(it);
            }
            if (stamps.size() > SWEEP_THRESHOLD) {
                stamps.clear();
            }
        }

        /// How many entries may accumulate before a sweep. Large enough that the common case — a
        /// few dozen resources loaded once — never sweeps at all.
        static constexpr std::size_t SWEEP_THRESHOLD = 256;
    };

    ResourceCache::ResourceCache() : _impl(std::make_unique<Impl>()) {
    }

    ResourceCache::~ResourceCache() = default;

    ResourceCache &ResourceCache::instance() {
        static ResourceCache cache;
        return cache;
    }

    std::size_t ResourceCache::parseCount() const {
        std::shared_lock lock(_impl->mutex);
        return _impl->parses;
    }

    std::size_t ResourceCache::entryCount() const {
        std::shared_lock lock(_impl->mutex);
        return _impl->entries.size();
    }

    void ResourceCache::clear() {
        std::unique_lock lock(_impl->mutex);
        _impl->entries.clear();
        _impl->stamps.clear();
        _impl->parses = 0;
    }

    srt::Expected<std::shared_ptr<const void>> ResourceCache::acquireErased(
        const fs::path &path, std::string_view kind, std::string_view parserGeneration,
        const std::function<srt::Expected<std::shared_ptr<const void>>(const fs::path &)> &parse) {
        std::error_code error;
        auto resolved = fs::weakly_canonical(path, error);
        if (error) {
            resolved = fs::absolute(path).lexically_normal();
        }

        auto stat = statFile(resolved);
        if (!stat) {
            return stat.takeError();
        }

        Digest digest{};
        bool haveDigest = false;
        {
            std::shared_lock lock(_impl->mutex);
            if (const auto it = _impl->stamps.find(resolved); it != _impl->stamps.end()) {
                // Trusting size and timestamp means a rewrite that preserves both reads as
                // unchanged. That breaks the promise a Package makes about its own contents, and
                // detecting it is not this cache's job.
                if (it->second.size == stat->first && it->second.modified == stat->second) {
                    digest = it->second.digest;
                    haveDigest = true;
                }
            }
        }
        if (!haveDigest) {
            auto computed = hashFile(resolved);
            if (!computed) {
                return computed.takeError();
            }
            digest = computed.take();
        }

        const auto key = std::make_tuple(digest, std::string(kind), std::string(parserGeneration));
        {
            std::shared_lock lock(_impl->mutex);
            if (const auto it = _impl->entries.find(key); it != _impl->entries.end()) {
                if (auto live = it->second.lock()) {
                    return live;
                }
            }
        }

        // Checked again under the exclusive lock: two executives starting together must not both
        // parse the same resource. The one that finds neither an entry nor a parse under way
        // takes the parse; the others wait on its outcome, outside the lock.
        std::promise<Impl::Outcome> promise;
        std::shared_future<Impl::Outcome> outcome;
        bool parser = false;
        {
            std::unique_lock lock(_impl->mutex);
            if (const auto it = _impl->entries.find(key); it != _impl->entries.end()) {
                if (auto live = it->second.lock()) {
                    return live;
                }
            }
            if (const auto it = _impl->inflight.find(key); it != _impl->inflight.end()) {
                outcome = it->second;
            } else {
                outcome = promise.get_future().share();
                _impl->inflight.emplace(key, outcome);
                parser = true;
            }
        }
        if (!parser) {
            const auto &settled = outcome.get();
            if (settled.error) {
                return *settled.error;
            }
            return settled.value;
        }

        Impl::Outcome settled;
        auto parsed = parse(resolved);
        if (!parsed) {
            settled.error = parsed.takeError();
        } else {
            // The parse product's control block is emitted into the interpreter plugin that built
            // it, and a plugin is unloaded with the Runtime that loaded it while this cache lives
            // for the whole process. A weak reference to that control block would therefore have
            // to be released through code that is no longer mapped. Re-owning the value behind a
            // control block of this library moves that release here: the plugin's reference is
            // dropped by the deleter, which runs when the value itself dies, and the value dies
            // before its plugin.
            auto fromPlugin = parsed.take();
            // The address is read before the constructor arguments are built: capturing by move
            // and calling get() in the same expression would leave their order to the compiler.
            auto address = fromPlugin.get();
            settled.value = std::shared_ptr<const void>(
                address, [held = std::move(fromPlugin)](const void *) mutable { held.reset(); });
        }
        {
            std::unique_lock lock(_impl->mutex);
            _impl->inflight.erase(key);
            if (settled.value) {
                if (_impl->entries.size() >= Impl::SWEEP_THRESHOLD) {
                    _impl->sweep();
                }
                _impl->entries[key] = settled.value;
                _impl->stamps[resolved] = Stamp{stat->first, stat->second, digest};
                ++_impl->parses;
            }
        }
        promise.set_value(settled);
        if (settled.error) {
            return *settled.error;
        }
        return settled.value;
    }

}
