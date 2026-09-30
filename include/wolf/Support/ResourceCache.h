#ifndef WOLF_RESOURCECACHE_H
#define WOLF_RESOURCECACHE_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Support/Expected.h>

#include <wolf/Support/SupportGlobal.h>

namespace wolf {

    /// Shares parsed read-only resources across executives.
    ///
    /// The cache is a functional requirement, not an optimization. One executive runs one
    /// conversion, and a host that requires k concurrent conversions creates k chains. Without
    /// sharing, k chains hold k copies of every dictionary and model that the chain reads.
    ///
    /// Entries are addressed by content, and two packages that ship the same dictionary under
    /// different paths therefore share one parse. A parsed value must be independent of the base
    /// path. It must not capture the directory from which it was read, because two packages that
    /// share an entry would otherwise resolve relative references against the package that loaded
    /// first.
    ///
    /// The cache holds weak references, and an entry is destroyed with the last module that uses
    /// it. The cache belongs to the provider execution domain instead of to a Package, which
    /// allows it to outlive an individual load.
    class WOLF_INTERNAL_EXPORT ResourceCache {
    public:
        /// Returns the single cache of the current provider domain.
        static ResourceCache &instance();

        /// Returns the parsed form of \a path, parsing it only if no live entry matches.
        ///
        /// \a kind names the resource type within a variant. The same file parsed by two
        /// different families yields two entries that are never shared, because a dictionary read
        /// leniently and the same dictionary read strictly do not produce interchangeable values.
        ///
        /// \a parserGeneration is the syntax generation of the parser, so that entries of an
        /// earlier format no longer match after a format change without explicit invalidation.
        template <class T>
        srt::Expected<std::shared_ptr<const T>>
            acquire(const std::filesystem::path &path, std::string_view kind,
                    std::string_view parserGeneration,
                    const std::function<srt::Expected<std::shared_ptr<const T>>(
                        const std::filesystem::path &)> &parse) {
            auto entry = acquireErased(
                path, kind, parserGeneration,
                [&parse](const std::filesystem::path &resolved)
                    -> srt::Expected<std::shared_ptr<const void>> {
                    auto parsed = parse(resolved);
                    if (!parsed) {
                        return parsed.takeError();
                    }
                    return std::static_pointer_cast<const void>(parsed.take());
                });
            if (!entry) {
                return entry.takeError();
            }
            return std::static_pointer_cast<const T>(entry.take());
        }

    private:
        ResourceCache();
        ~ResourceCache();

        srt::Expected<std::shared_ptr<const void>>
            acquireErased(const std::filesystem::path &path, std::string_view kind,
                          std::string_view parserGeneration,
                          const std::function<srt::Expected<std::shared_ptr<const void>>(
                              const std::filesystem::path &)> &parse);

        class Impl;
        std::unique_ptr<Impl> _impl;

        /// Sharing and reclamation are observable only through entry counts. The resource cache
        /// test therefore reads the index through this class, which the test defines against the
        /// private ResourceCacheImpl.h. The library contains no definition of this class.
        friend class ResourceCacheProbe;
    };

}

#endif // WOLF_RESOURCECACHE_H
