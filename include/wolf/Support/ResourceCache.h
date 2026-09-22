#ifndef WOLF_RESOURCECACHE_H
#define WOLF_RESOURCECACHE_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <synthrt/Support/Expected.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Shares parsed read-only resources across executives.
    ///
    /// This is not an optimization. One executive carries one conversion, so a host that wants k
    /// conversions in flight creates k chains; without sharing, k chains means k copies of every
    /// dictionary and model the chain reads.
    ///
    /// Entries are addressed by content, so two packages shipping the same dictionary under
    /// different paths share one parse. A parsed value must be base-path clean: it may not capture
    /// the directory it was read from, or two packages sharing an entry would silently resolve
    /// relative references against whichever of them loaded first.
    ///
    /// The cache holds weak references, so an entry dies with the last module that uses it. It
    /// belongs to the provider execution domain rather than to any Package, which is what lets it
    /// outlive an individual load.
    class WOLF_EXPORT ResourceCache {
    public:
        /// The one cache of the current provider domain.
        static ResourceCache &instance();

        /// Returns the parsed form of \a path, parsing it only if no live entry matches.
        ///
        /// \a kind names the resource type within a variant. The same file parsed by two different
        /// families is two entries and never shared: a dictionary read leniently and the same
        /// dictionary read strictly do not produce interchangeable values.
        ///
        /// \a parserGeneration is the syntax generation of the parser, so that a format change
        /// stops matching old entries without anyone having to invalidate them.
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

        /// Number of parses performed. Test scaffolding: sharing is only observable by counting.
        std::size_t parseCount() const;

        /// Number of entries the index holds, live and not yet reclaimed. Test scaffolding: an
        /// index that only ever grew is only observable by counting.
        std::size_t entryCount() const;

        /// Drops every live entry and the path memo. Test scaffolding.
        void clear();

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
    };

}

#endif // WOLF_RESOURCECACHE_H
