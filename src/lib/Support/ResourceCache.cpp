#include "ResourceCache.h"
#include "ResourceCacheImpl.h"

#include "Files.h"

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

        using resourcecache::Digest;
        using resourcecache::Stamp;

        static_assert(std::tuple_size_v<Digest> == BLAKE3_OUT_LEN,
                      "the digest type of ResourceCacheImpl.h must hold a BLAKE3 hash");

        /// Chunk size of one hash read. The resources reach several megabytes, so a whole file is
        /// never held in memory. A buffer of this size keeps the read loop short at a negligible
        /// memory cost.
        constexpr std::size_t HASH_CHUNK_BYTES = 64 * 1024;

        /// Returns an error that distinguishes a missing resource from an existing resource that
        /// cannot be read.
        ///
        /// This check runs before a parser reads the file, and the two faults are therefore
        /// distinguished at this layer by the rule that every reader in wolf applies
        /// (fileErrorCode). The message uses the wording of this layer, which names the failed
        /// step.
        srt::Error unreadable(const fs::path &path, const std::string &what) {
            return srt::Error(fileErrorCode(path), stdc::path::to_utf8(path) + ": " + what);
        }

        /// Streams a file through the hash instead of holding it in memory, because these
        /// resources reach hundreds of thousands of lines.
        srt::Expected<Digest> hashFile(const fs::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return unreadable(path, "failed to open a resource for hashing");
            }
            blake3_hasher hasher;
            blake3_hasher_init(&hasher);
            std::vector<char> buffer(HASH_CHUNK_BYTES);
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

        srt::Expected<std::pair<std::uintmax_t, fs::file_time_type>>
            statFile(const fs::path &path) {
            std::error_code error;
            const auto size = fs::file_size(path, error);
            if (error) {
                return unreadable(path, "failed to size a resource");
            }
            const auto modified = fs::last_write_time(path, error);
            if (error) {
                return unreadable(path, "failed to stat a resource");
            }
            return std::make_pair(size, modified);
        }

    }

    ResourceCache::ResourceCache() : _impl(std::make_unique<Impl>()) {
    }

    ResourceCache::~ResourceCache() = default;

    ResourceCache &ResourceCache::instance() {
        static ResourceCache cache;
        return cache;
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
                // Relying on size and timestamp treats a rewrite that preserves both as unchanged.
                // Such a rewrite violates the immutability of Package contents, and detecting it is
                // outside the responsibility of this cache.
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

        // Checked again under the exclusive lock, because two executives that start at the same
        // time must not both parse the same resource. The executive that finds neither an entry
        // nor a parse in progress performs the parse, and the others wait for its outcome outside
        // the lock.
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

        // From this point every path settles the parse, including an exception thrown by parse(),
        // because the destructor of the settlement clears the key and notifies the waiters.
        Impl::Settlement settlement(*_impl, key, promise, resolved);
        Impl::Outcome settled;
        auto parsed = parse(resolved);
        if (!parsed) {
            settled.error = parsed.takeError();
        } else {
            // The control block of the parse product is emitted into the interpreter plugin that
            // created it. A plugin is unloaded with the Runtime that loaded it, while this cache
            // exists for the whole process. A weak reference to that control block would
            // therefore have to be released through code that is no longer mapped. Re-owning the
            // value behind a control block of this library moves the release into this library.
            // The deleter drops the reference of the plugin when the value is destroyed, and the
            // value is destroyed before its plugin.
            auto fromPlugin = parsed.take();
            // The address is read before the constructor arguments are built, because capturing
            // by move and calling get() in the same expression leaves the evaluation order
            // unspecified.
            auto address = fromPlugin.get();
            settled.value = std::shared_ptr<const void>(
                address, [held = std::move(fromPlugin)](const void *) mutable { held.reset(); });
        }
        settlement.settle(settled,
                          std::make_pair(resolved, Stamp{stat->first, stat->second, digest}));
        if (settled.error) {
            return *settled.error;
        }
        return settled.value;
    }

}
