#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <wolf/Support/ResourceCache.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

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

/// The acceptance criterion of the whole design: two holders of the same resource cost one parse.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ParsesSharedResourceOnce) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

    const auto path = writeTemp("shared.txt", "a\tb\n");
    auto first = acquire(path, "dict-tsv");
    auto second = acquire(path, "dict-tsv");
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);

    BOOST_CHECK_EQUAL(cache.parseCount(), 1u);
    BOOST_CHECK(first->get() == second->get());
    BOOST_CHECK_EQUAL((*first)->text, "a\tb\n");
}

/// Addressing by content rather than by path is what makes an override that copies a resource
/// verbatim cost nothing extra.
BOOST_AUTO_TEST_CASE(test_ResourceCache_SharesAcrossPathsWithEqualContent) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

    auto one = acquire(writeTemp("copy-a.txt", "same"), "dict-tsv");
    auto two = acquire(writeTemp("copy-b.txt", "same"), "dict-tsv");
    BOOST_REQUIRE(one);
    BOOST_REQUIRE(two);

    BOOST_CHECK_EQUAL(cache.parseCount(), 1u);
    BOOST_CHECK(one->get() == two->get());
}

/// The same file read by two families is two entries. A dictionary parsed leniently and the same
/// dictionary parsed strictly do not produce interchangeable values, so they must not share.
BOOST_AUTO_TEST_CASE(test_ResourceCache_SeparatesKindsAndGenerations) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

    const auto path = writeTemp("kinds.txt", "x");
    auto lenient = acquire(path, "dsdict-tsv");
    auto strict = acquire(path, "s2p-dict-tsv");
    auto newer = acquire(path, "dsdict-tsv", "test@2");
    BOOST_REQUIRE(lenient);
    BOOST_REQUIRE(strict);
    BOOST_REQUIRE(newer);

    BOOST_CHECK_EQUAL(cache.parseCount(), 3u);
    BOOST_CHECK(lenient->get() != strict->get());
    BOOST_CHECK(lenient->get() != newer->get());
}

/// Entries are held weakly, so one dies with its last holder instead of pinning memory for the
/// life of the process.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ReleasesWithItsLastHolder) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

    const auto path = writeTemp("weak.txt", "y");
    {
        auto held = acquire(path, "dict-tsv");
        BOOST_REQUIRE(held);
        BOOST_CHECK_EQUAL(cache.parseCount(), 1u);
    }
    auto again = acquire(path, "dict-tsv");
    BOOST_REQUIRE(again);
    BOOST_CHECK_EQUAL(cache.parseCount(), 2u);
}

/// Executives are created in parallel by hosts that warm several languages at once, so the
/// double-checked insert has to hold under contention.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ParsesOnceUnderContention) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

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

    BOOST_CHECK_EQUAL(cache.parseCount(), 1u);
    for (const auto &value : held) {
        BOOST_CHECK(value == held.front());
    }
}

/// The index must not grow without limit across a session of loading and unloading.
///
/// Entries are weak, so a value dies with its last holder — but the entry itself stayed, one map
/// node and one control block each. A host that loads and unloads packages all session long, which
/// is what editing a project looks like, accumulated them for the life of the process.
///
/// Each round uses a path of its own. Reusing one path would let the stamp memo answer with the
/// previous digest whenever the rewrite happened to preserve both size and modification time, so
/// most rounds would collapse onto one entry and the case would measure nothing.
BOOST_AUTO_TEST_CASE(test_ResourceCache_ReclaimsDeadEntries) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

    // Well past the sweep threshold, each with a path and content of its own.
    constexpr int COUNT = 600;
    std::vector<fs::path> written;
    for (int i = 0; i < COUNT; ++i) {
        const auto name = "churn-" + std::to_string(i) + ".txt";
        const auto path = writeTemp(name, "content-" + std::to_string(i) + "\n");
        written.push_back(path);
        auto held = acquire(path, "dict-tsv");
        BOOST_REQUIRE(held);
        // held dies here, so the entry is dead by the next round.
    }
    for (const auto &path : written) {
        fs::remove(path);
    }

    BOOST_TEST_MESSAGE("entries=" << cache.entryCount() << " parses=" << cache.parseCount());
    BOOST_CHECK_EQUAL(cache.parseCount(), static_cast<std::size_t>(COUNT));
    BOOST_CHECK_MESSAGE(cache.entryCount() < static_cast<std::size_t>(COUNT),
                        "the index kept every dead entry: " + std::to_string(cache.entryCount()));
    cache.clear();
}

/// A resource that cannot be read has to say which one it was.
///
/// This message leaves the layer and reaches a person through the host, which prints "could not
/// open <package>: failed to interpret module configuration: ...". With nothing but "failed to
/// size a resource" after the last colon, the one thing the reader needs is the one thing missing,
/// and the path was in hand when the message was written.
BOOST_AUTO_TEST_CASE(test_ResourceCache_NamesTheResourceItCannotRead) {
    auto &cache = wolf::ResourceCache::instance();
    cache.clear();

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
