#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <wolf/Support/ResourceCache.h>

// The private state of the cache, used by the probe below. The build adds src/lib/Support to the
// include path of this test and of no other target.
#include "ResourceCacheImpl.h"

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace wolf {

    /// Reads the index of the process-wide cache. Sharing and reclamation are observable only by
    /// counting, and the library exports no interface for counting: ResourceCache declares this
    /// class as a friend, and the class is defined only in this test.
    class ResourceCacheProbe {
    public:
        static std::size_t parseCount() {
            auto &impl = *ResourceCache::instance()._impl;
            std::shared_lock lock(impl.mutex);
            return impl.parses;
        }

        static std::size_t entryCount() {
            auto &impl = *ResourceCache::instance()._impl;
            std::shared_lock lock(impl.mutex);
            return impl.entries.size();
        }

        /// Discards every live entry and the path memo, so that each case starts from an empty
        /// cache.
        static void clear() {
            auto &impl = *ResourceCache::instance()._impl;
            std::unique_lock lock(impl.mutex);
            impl.entries.clear();
            impl.stamps.clear();
            impl.parses = 0;
            impl.insertions = 0;
        }
    };

}

using Probe = wolf::ResourceCacheProbe;

namespace {

    struct Parsed {
        std::string text;
    };

    fs::path writeTemp(const std::string &name, const std::string &content) {
        const auto path = fs::temp_directory_path() / ("wolf-cache-test-" + name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << content;
        return path;
    }

    srt::Expected<std::shared_ptr<const Parsed>> parseFile(const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        return std::shared_ptr<const Parsed>(new Parsed{std::move(content)});
    }

    srt::Expected<std::shared_ptr<const Parsed>> acquire(const fs::path &path, const char *kind,
                                                         const char *generation = "test@1") {
        return wolf::ResourceCache::instance().acquire<Parsed>(path, kind, generation,
                                                               std::function(parseFile));
    }

}

BOOST_AUTO_TEST_SUITE(test_ResourceCache)

/// Checks the acceptance criterion of the design: two holders of the same resource cost one parse.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ParsesSharedResourceOnce) {
    Probe::clear();

    const auto path = writeTemp("shared.txt", "a\tb\n");
    auto first = acquire(path, "dict-tsv");
    auto second = acquire(path, "dict-tsv");
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);

    BOOST_CHECK_EQUAL(Probe::parseCount(), 1u);
    BOOST_CHECK(first->get() == second->get());
    BOOST_CHECK_EQUAL((*first)->text, "a\tb\n");
}

/// Addressing by content rather than by path ensures that an override which copies a resource
/// verbatim incurs no additional cost.
BOOST_AUTO_TEST_CASE(test_ResourceCache_SharesAcrossPathsWithEqualContent) {
    Probe::clear();

    auto one = acquire(writeTemp("copy-a.txt", "same"), "dict-tsv");
    auto two = acquire(writeTemp("copy-b.txt", "same"), "dict-tsv");
    BOOST_REQUIRE(one);
    BOOST_REQUIRE(two);

    BOOST_CHECK_EQUAL(Probe::parseCount(), 1u);
    BOOST_CHECK(one->get() == two->get());
}

/// The same file read by two families produces two entries. A dictionary parsed leniently and the
/// same dictionary parsed strictly do not produce interchangeable values, so the two values must
/// not share an entry.
BOOST_AUTO_TEST_CASE(test_ResourceCache_SeparatesKindsAndGenerations) {
    Probe::clear();

    const auto path = writeTemp("kinds.txt", "x");
    auto lenient = acquire(path, "dsdict-tsv");
    auto strict = acquire(path, "s2p-dict-tsv");
    auto newer = acquire(path, "dsdict-tsv", "test@2");
    BOOST_REQUIRE(lenient);
    BOOST_REQUIRE(strict);
    BOOST_REQUIRE(newer);

    BOOST_CHECK_EQUAL(Probe::parseCount(), 3u);
    BOOST_CHECK(lenient->get() != strict->get());
    BOOST_CHECK(lenient->get() != newer->get());
}

/// Entries are held weakly, so an entry is destroyed with its last holder instead of retaining
/// memory for the lifetime of the process.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ReleasesWithItsLastHolder) {
    Probe::clear();

    const auto path = writeTemp("weak.txt", "y");
    {
        auto held = acquire(path, "dict-tsv");
        BOOST_REQUIRE(held);
        BOOST_CHECK_EQUAL(Probe::parseCount(), 1u);
    }
    auto again = acquire(path, "dict-tsv");
    BOOST_REQUIRE(again);
    BOOST_CHECK_EQUAL(Probe::parseCount(), 2u);
}

/// Hosts that warm several languages at once create executives in parallel, so the double-checked
/// insert must remain correct under contention.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ParsesOnceUnderContention) {
    Probe::clear();

    const auto path = writeTemp("racy.txt", "z");
    std::vector<std::shared_ptr<const Parsed>> held(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < held.size(); ++i) {
        threads.emplace_back([&held, &path, i]() {
            auto value = acquire(path, "dict-tsv");
            if (value) {
                held[i] = value.take();
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    BOOST_CHECK_EQUAL(Probe::parseCount(), 1u);
    for (const auto &value : held) {
        BOOST_CHECK(value == held.front());
    }
}

/// The index must not grow without limit across a session of loading and unloading.
///
/// Entries are weak, so a value is destroyed with its last holder, but the entry itself previously
/// remained, occupying one map node and one control block. A host that loads and unloads packages
/// throughout a session, as happens while a project is edited, accumulated such entries for the
/// lifetime of the process.
///
/// Each round uses a separate path. Reusing one path would allow the stamp memo to return the
/// previous digest whenever the rewrite preserved both size and modification time, so most rounds
/// would collapse onto one entry and the case would measure nothing.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ReclaimsDeadEntries) {
    Probe::clear();

    // The count is well above the sweep threshold, and each file has its own path and content.
    constexpr int COUNT = 600;
    std::vector<fs::path> written;
    for (int i = 0; i < COUNT; ++i) {
        const auto name = "churn-" + std::to_string(i) + ".txt";
        const auto path = writeTemp(name, "content-" + std::to_string(i) + "\n");
        written.push_back(path);
        auto held = acquire(path, "dict-tsv");
        BOOST_REQUIRE(held);
        // held is destroyed here, so the entry is dead before the next round.
    }
    for (const auto &path : written) {
        fs::remove(path);
    }

    BOOST_TEST_MESSAGE("entries=" << Probe::entryCount() << " parses=" << Probe::parseCount());
    BOOST_CHECK_EQUAL(Probe::parseCount(), static_cast<std::size_t>(COUNT));
    BOOST_CHECK_MESSAGE(Probe::entryCount() < static_cast<std::size_t>(COUNT),
                        "the index kept every dead entry: " + std::to_string(Probe::entryCount()));
    Probe::clear();
}

/// A failed parse is reported to every executive that waited on it, with the error code of the
/// parser, and leaves no state behind: the next acquisition of the same resource parses again.
BOOST_AUTO_TEST_CASE(test_ResourceCache_HandsAFailureToEveryWaiter) {
    auto &cache = wolf::ResourceCache::instance();
    Probe::clear();

    const auto path = writeTemp("fails.txt", "f");
    std::atomic<int> attempts{0};
    const std::function<srt::Expected<std::shared_ptr<const Parsed>>(const fs::path &)> failing =
        [&attempts](const fs::path &) -> srt::Expected<std::shared_ptr<const Parsed>> {
        ++attempts;
        // The delay is long enough for the other threads to find the parse in progress and wait
        // on it.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return srt::Error(srt::Error::FeatureNotSupported, "this parser rejects every input");
    };

    constexpr int THREADS = 6;
    std::atomic<int> refused{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&] {
            auto value = cache.acquire<Parsed>(path, "failing", "test@1", failing);
            if (!value && value.error().code() == srt::Error::FeatureNotSupported) {
                ++refused;
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    BOOST_CHECK_EQUAL(refused.load(), THREADS);
    BOOST_CHECK_EQUAL(Probe::parseCount(), 0u);

    // Nothing was published, and no parse is still in progress.
    auto good = cache.acquire<Parsed>(path, "failing", "test@1", std::function(parseFile));
    BOOST_REQUIRE(good);
    BOOST_CHECK_EQUAL((*good)->text, "f");
    fs::remove(path);
}

/// A parser that throws must not leave its resource permanently in progress. The key previously
/// remained in the in-flight index, so every later acquisition of that resource waited on a
/// promise that would never be fulfilled, and the executives already waiting received a broken
/// promise instead of an error.
BOOST_AUTO_TEST_CASE(test_ResourceCache_SurvivesAParserThatThrows) {
    auto &cache = wolf::ResourceCache::instance();
    Probe::clear();

    const auto path = writeTemp("throws.txt", "t");
    const std::function<srt::Expected<std::shared_ptr<const Parsed>>(const fs::path &)> throwing =
        [](const fs::path &) -> srt::Expected<std::shared_ptr<const Parsed>> {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        throw std::runtime_error("the parser gave up");
    };

    constexpr int THREADS = 6;
    std::atomic<int> thrown{0};
    std::atomic<int> refused{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&] {
            try {
                auto value = cache.acquire<Parsed>(path, "throwing", "test@1", throwing);
                if (!value) {
                    ++refused;
                }
            } catch (const std::runtime_error &) {
                // The thread that ran the parser receives the exception directly.
                ++thrown;
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    // Every thread returned, either with the exception or with an error that names it.
    BOOST_CHECK_GE(thrown.load(), 1);
    BOOST_CHECK_EQUAL(thrown.load() + refused.load(), THREADS);

    auto good = cache.acquire<Parsed>(path, "throwing", "test@1", std::function(parseFile));
    BOOST_REQUIRE_MESSAGE(good, "the resource remained in progress after its parser threw");
    BOOST_CHECK_EQUAL((*good)->text, "t");
    fs::remove(path);
}

/// The error for a resource that cannot be read must name the resource.
///
/// This message leaves the layer and reaches the user through the host, which prints "could not
/// open <package>: failed to interpret module configuration: ...". If only "failed to size a
/// resource" followed the last colon, the message would omit the path, which is the information
/// the user requires, although the path is available when the message is constructed.
BOOST_AUTO_TEST_CASE(test_ResourceCache_NamesTheResourceItCannotRead) {
    Probe::clear();

    const auto missing = fs::temp_directory_path() / "wolf-cache-test-not-here.txt";
    fs::remove(missing);

    auto absent = acquire(missing, "dict-tsv");
    BOOST_REQUIRE(!absent);

    const auto text = absent.error().toString();
    BOOST_CHECK_MESSAGE(text.find("failed to size a resource") != std::string::npos,
                        "the reason went missing: " + text);
    BOOST_CHECK_MESSAGE(text.find("wolf-cache-test-not-here.txt") != std::string::npos,
                        "the file went unnamed: " + text);
}

BOOST_AUTO_TEST_SUITE_END()
