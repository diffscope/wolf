#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Support/Error.h>

#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Session/LinguistSession.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace LinguistApi = wolf::Api::Linguist::L1;

namespace {

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit,
                              {wolf::test::fixtureRoot()});
    }

    /// Loads the passthrough singer and returns its package handle. The handle must outlive every
    /// use of the package because the handle keeps the package, and therefore the specs, alive.
    srt::PackageHandle load(srt::SynthUnit &unit, const char *name) {
        return wolf::test::load(unit, wolf::test::fixtureRoot() / name);
    }

    wolf::SingerRef refOf(const srt::PackageHandle &package, const char *id) {
        return {srt::ContribLocator(package.id(), "singer", id), package.version()};
    }

    /// A conversion input is a task payload, which is neither copyable nor movable, so the input is
    /// constructed in place and passed by reference.
    struct Line {
        LinguistApi::LinguistConvertInput input;

        Line(std::initializer_list<const char *> lyrics, LinguistApi::Depth depth) {
            input.depth = depth;
            for (const auto *lyric : lyrics) {
                input.words.push_back({lyric, std::nullopt, std::nullopt});
            }
        }

        operator const LinguistApi::LinguistConvertInput &() const {
            return input;
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_LinguistSession)

/// The catalog provides the information that a host requires before any conversion: the available
/// singers, the languages that each singer declares, and the binding of each language.
BOOST_AUTO_TEST_CASE(test_LinguistSession_CatalogsWhatIsLoaded) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    auto catalog = session.catalog();
    BOOST_REQUIRE_EQUAL(catalog->singers().size(), 1u);

    const auto &singer = catalog->singers().front();
    BOOST_CHECK_EQUAL(singer.ref.locator.contributionId(), "s");
    BOOST_CHECK_EQUAL(singer.defaultLanguage, "zxx");
    BOOST_REQUIRE_EQUAL(singer.languages.size(), 1u);
    BOOST_CHECK_EQUAL(singer.languages[0].handle, "zxx");
    BOOST_CHECK_EQUAL(singer.languages[0].binding.language, "zxx");
    BOOST_CHECK_EQUAL(singer.languages[0].binding.scheme, "passthrough");
    BOOST_CHECK_EQUAL(singer.languages[0].linguist.contributionId(), "zxx-passthrough");

    // A round trip through a catalog entry must find the same entry.
    BOOST_CHECK(catalog->find(singer.ref) == &singer);

    // A catalog is a snapshot: it remains unchanged after the session state changes.
    session.refresh();
    BOOST_CHECK_EQUAL(catalog->singers().size(), 1u);
}

/// Cold indicates that a route exists and has not been built; Ready indicates that the next
/// conversion loads nothing. A readiness query must never trigger loading.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ProbeDoesNotWarm) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    // The status remains Cold after a hundred queries: a host that polls every frame must not
    // load a model.
    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    }

    BOOST_CHECK(session.warm(singer, "zxx"));
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Ready);

    // A refresh discards the readiness cache together with its catalog.
    session.refresh();
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
}

/// A language that the singer does not declare and a singer that does not exist are both
/// Unavailable, with a reason that a host can display rather than a bare failure.
BOOST_AUTO_TEST_CASE(test_LinguistSession_NamesWhyItIsUnavailable) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);

    const auto status = session.probe(refOf(package, "s"), "cmn");
    BOOST_CHECK(status.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK(!status.reason.empty());
    BOOST_CHECK(!session.warm(refOf(package, "s"), "cmn"));

    const auto missing = session.probe(refOf(package, "nobody"), "zxx");
    BOOST_CHECK(missing.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK(!missing.reason.empty());

    // Neither status may report a usable depth. No value represents the absence of any depth, so
    // an unavailable status carries the shallowest depth rather than the deepest: a caller that
    // reads the depth without checking readiness first then requests less than available instead
    // of more than exists. Regression: an unavailable status must not carry Onsets.
    BOOST_CHECK(status.maxDepth == LinguistApi::Depth::Pronunciation);
    BOOST_CHECK(missing.maxDepth == LinguistApi::Depth::Pronunciation);
}

/// Every entry point rejects an undeclared language in the same way, based on the catalog rather
/// than on a build attempt: a phrase of five hundred notes must not turn one rejection into five
/// hundred attempts. lite currently maintains this cache manually.
///
/// This case checks only the inexpensive part, a fact already recorded in the catalog. The other
/// part, a route that is declared but fails to build, requires a resource that fails only at
/// executive creation. No fixture here provides such a resource, so that part is not covered.
BOOST_AUTO_TEST_CASE(test_LinguistSession_RefusesAnUndeclaredLanguageEveryTime) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(!session.warm(singer, "cmn"));
    BOOST_CHECK(session.probe(singer, "cmn").readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK(!session.convert(singer, "cmn", Line({"1"}, LinguistApi::Depth::Onsets)));

    // Nothing was built: the declared language is still Cold, so the rejections did not warm
    // anything as a side effect.
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
}

/// Conversion keeps the L4 structures: the same input, the same per-word output, and the same
/// depth cut-offs.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ConvertsThroughThePool) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    auto result = session.convert(singer, "zxx", Line({"1", "2"}, LinguistApi::Depth::Onsets));
    if (!result) {
        BOOST_FAIL("the conversion should have run: " + result.error().toString());
    }
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "1");
    BOOST_CHECK_EQUAL((*result)->words[1].pronunciation, "2");

    // A conversion also warms the language: the session retains the executive that the
    // conversion built.
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Ready);

    // A second run reuses that executive rather than building another one. The reuse is
    // observable only in that the conversion still works after the executive was returned.
    auto again = session.convert(singer, "zxx", Line({"3"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(again);
    BOOST_CHECK_EQUAL((*again)->words[0].pronunciation, "3");
}

/// The session converts reserved markers itself and never passes them to a language, so the
/// session must produce their output structure at every depth. Otherwise an S2P dictionary would
/// receive a lookup for SP.
BOOST_AUTO_TEST_CASE(test_LinguistSession_AnswersReservedMarkersItself) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    auto onsets =
        session.convert(singer, "zxx", Line({"1", "SP", "AP", "2"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(onsets);
    const auto &words = (*onsets)->words;
    BOOST_REQUIRE_EQUAL(words.size(), 4u);

    for (const auto index : {1u, 2u}) {
        BOOST_CHECK(words[index].mode == wolf::Api::G2P::L1::Mode::Copy);
        BOOST_CHECK_EQUAL(words[index].pronunciation, index == 1u ? "SP" : "AP");
        BOOST_REQUIRE_EQUAL(words[index].phonemes.size(), 1u);
        BOOST_CHECK_EQUAL(words[index].phonemes[0], words[index].pronunciation);
        BOOST_REQUIRE_EQUAL(words[index].onsets.size(), 1u);
        BOOST_CHECK(words[index].onsets[0]);
    }
    // The ordinary words around the markers keep their positions.
    BOOST_CHECK_EQUAL(words[0].pronunciation, "1");
    BOOST_CHECK_EQUAL(words[3].pronunciation, "2");

    // At a shallower depth, a marker stops at the same layer as every other word.
    auto pronunciation =
        session.convert(singer, "zxx", Line({"SP"}, LinguistApi::Depth::Pronunciation));
    BOOST_REQUIRE(pronunciation);
    BOOST_CHECK_EQUAL((*pronunciation)->words[0].pronunciation, "SP");
    BOOST_CHECK((*pronunciation)->words[0].phonemes.empty());

    auto phonemes = session.convert(singer, "zxx", Line({"SP"}, LinguistApi::Depth::Phonemes));
    BOOST_REQUIRE(phonemes);
    BOOST_REQUIRE_EQUAL((*phonemes)->words[0].phonemes.size(), 1u);
    BOOST_CHECK((*phonemes)->words[0].onsets.empty());
}

/// A host that uses another notation replaces the marker set, and a pinned pronunciation expresses
/// an intent that the session does not override.
BOOST_AUTO_TEST_CASE(test_LinguistSession_MarkersAreAConventionNotAContract) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    session.setReservedMarkers({"rest"});
    auto swapped =
        session.convert(singer, "zxx", Line({"rest", "SP"}, LinguistApi::Depth::Pronunciation));
    BOOST_REQUIRE(swapped);
    BOOST_CHECK((*swapped)->words[0].mode == wolf::Api::G2P::L1::Mode::Copy);
    // SP is now an ordinary word, and the passthrough language treats it as such.
    BOOST_CHECK_EQUAL((*swapped)->words[1].pronunciation, "SP");

    session.setReservedMarkers({"SP", "AP"});
    LinguistApi::LinguistConvertInput pinned;
    pinned.depth = LinguistApi::Depth::Pronunciation;
    pinned.words.push_back({"SP", std::string("s p"), std::nullopt});
    auto result = session.convert(singer, "zxx", pinned);
    BOOST_REQUIRE(result);
    // The host supplied a pronunciation, so the marker table was not consulted.
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "s p");
}

/// The marker declaration of a singer (reservedPhonemes in the singer category) takes precedence
/// over the session-wide set and leaves the session-wide set unchanged for every other singer. No
/// caller passes the set to the session; the session reads it from the declaration.
BOOST_AUTO_TEST_CASE(test_LinguistSession_MarkersDeclaredBySingerWinOverTheSessionSet) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-markers");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    auto result =
        session.convert(singer, "zxx", Line({"EP", "SP"}, LinguistApi::Depth::Pronunciation));
    BOOST_REQUIRE(result);
    BOOST_CHECK((*result)->words[0].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "EP");
    // SP is no longer a marker for this singer, so the passthrough language receives it as a word.
    BOOST_CHECK_EQUAL((*result)->words[1].pronunciation, "SP");
    BOOST_CHECK_EQUAL(session.reservedMarkers().size(), 2u);
}

/// A line that consists only of markers never reaches a language and is still returned complete.
BOOST_AUTO_TEST_CASE(test_LinguistSession_HandlesALineOfOnlyMarkers) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    auto result =
        session.convert(refOf(package, "s"), "zxx", Line({"SP", "AP"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "SP");
    BOOST_CHECK_EQUAL((*result)->words[1].pronunciation, "AP");
}

/// Installing a package while the user is editing is a common operation, so a refresh must be safe
/// during a running conversion. The pool advances to a new generation instead of emptying: idle
/// executives are destroyed immediately, and borrowed executives are destroyed when returned.
BOOST_AUTO_TEST_CASE(test_LinguistSession_RefreshesUnderAConversion) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    std::atomic<bool> stop{false};
    std::atomic<int> converted{0};
    std::thread worker([&] {
        while (!stop.load()) {
            auto result =
                session.convert(singer, "zxx", Line({"1", "2", "3"}, LinguistApi::Depth::Onsets));
            if (result && (*result)->words.size() == 3u) {
                ++converted;
            }
        }
    });

    // Waits until the worker has converted at least one line before refreshing. Without this
    // wait, the case contains a race: on a loaded machine, the main thread can finish all two
    // hundred refreshes before the worker is scheduled, and the case then measures nothing while
    // reporting a pass, or, as observed, a spurious failure.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (converted.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    BOOST_CHECK_MESSAGE(converted.load() > 0,
                        "the worker converted nothing before the refreshes");

    for (int i = 0; i < 200; ++i) {
        session.refresh();
    }
    stop = true;
    worker.join();

    // The worker continued across the refreshes rather than stopping at the first one.
    BOOST_CHECK_GT(converted.load(), 1);
    // The session remains usable afterwards, which would not be the case after a double free.
    BOOST_CHECK(session.convert(singer, "zxx", Line({"4"}, LinguistApi::Depth::Onsets)));
}

/// Releasing one singer discards its pool entry and its package handle and leaves all other state
/// unchanged. A host requires this release point before unloading a voicebank.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReleasesOneSinger) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(session.warm(singer, "zxx"));
    session.release(singer);
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    // The singer remains in the catalog: a release frees resources but does not unload the
    // package.
    BOOST_CHECK(session.catalog()->find(singer) != nullptr);
    BOOST_CHECK(session.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets)));
}

/// Releasing a singer while a conversion holds one of its executives must neither destroy that
/// executive during the conversion nor prevent the session from serving the singer afterwards.
/// The slot discarded by a release is destroyed outside the session lock, and a borrowed
/// executive is destroyed only when returned, following the same generation rule as refresh().
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReleasesUnderAConversion) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    std::atomic<bool> stop{false};
    std::atomic<int> converted{0};
    std::thread worker([&] {
        while (!stop.load()) {
            auto result =
                session.convert(singer, "zxx", Line({"1", "2", "3"}, LinguistApi::Depth::Onsets));
            if (result && (*result)->words.size() == 3u) {
                ++converted;
            }
        }
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (converted.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    BOOST_CHECK_MESSAGE(converted.load() > 0,
                        "the worker converted nothing before the releases");

    for (int i = 0; i < 200; ++i) {
        session.release(singer);
        // A lock held across the teardown would delay this intermediate probe() call.
        BOOST_CHECK(session.probe(singer, "zxx").readiness != wolf::Readiness::Unavailable);
    }
    stop = true;
    worker.join();

    BOOST_CHECK_GT(converted.load(), 1);
    BOOST_CHECK(session.convert(singer, "zxx", Line({"4"}, LinguistApi::Depth::Onsets)));
}

/// A token set before the conversion starts cancels the conversion before it runs.
BOOST_AUTO_TEST_CASE(test_LinguistSession_HonoursACancelledToken) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    wolf::CancelToken token;
    token.cancel();
    BOOST_CHECK(token.cancelled());

    const Line line({"1", "2"}, LinguistApi::Depth::Onsets);
    auto result = session.convert(singer, "zxx", line, token);
    BOOST_REQUIRE(result);
    // The batch never ran, so every word is returned unconverted. This assertion is the purpose of
    // the case: an earlier version stopped the executive and then started it regardless, and the
    // executive cleared the request on entry, so a token cancelled before convert() had no effect
    // and the whole chain ran to onsets.
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);
    for (const auto &word : (*result)->words) {
        BOOST_CHECK(word.pronunciation.empty());
        BOOST_CHECK(word.phonemes.empty());
        BOOST_CHECK(word.onsets.empty());
    }
    // A cancellation is not a failure of the route: this batch was dropped before it began, and
    // the language must not be reported as broken afterwards.
    BOOST_CHECK(session.probe(singer, "zxx").readiness != wolf::Readiness::Unavailable);

    // The session remains usable: a conversion without cancellation still works.
    BOOST_CHECK(session.convert(singer, "zxx", Line({"3"}, LinguistApi::Depth::Onsets)));
}

/// A marker note whose phoneme layer the user pinned keeps that layer.
///
/// The session converts reserved markers itself instead of sending them to a language, but a pin
/// takes precedence: an earlier version checked only for a pinned pronunciation, so a manual edit
/// of the phonemes of SP was discarded and replaced by the marker.
BOOST_AUTO_TEST_CASE(test_LinguistSession_KeepsAPinnedLayerOnAReservedMarker) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Onsets;
    LinguistApi::LockedPhonemes pinned;
    pinned.phonemes = {"s", "il"};
    pinned.onsets = {true, false};
    input.words.push_back({"SP", std::nullopt, pinned});

    auto result = session.convert(singer, "zxx", input);
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);

    const auto &word = (*result)->words.front();
    BOOST_CHECK(word.phonemes == std::vector<std::string>({"s", "il"}));
    BOOST_CHECK(word.onsets == std::vector<bool>({true, false}));
    BOOST_CHECK(word.hitStage == LinguistApi::HitStage::Locked);

    // Without a pinned layer, the session still converts the marker as before.
    LinguistApi::LinguistConvertInput plain;
    plain.depth = LinguistApi::Depth::Onsets;
    plain.words.push_back({"SP", std::nullopt, std::nullopt});
    auto marker = session.convert(singer, "zxx", plain);
    BOOST_REQUIRE(marker);
    BOOST_CHECK((*marker)->words.front().phonemes == std::vector<std::string>({"SP"}));
}

/// The pool allows several conversions to run concurrently, and an executive runs one execution at
/// a time. Many threads on one singer and one language must work: each thread receives its own
/// executive, and returning an executive must not pass it to a thread still using it.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ConvertsInParallel) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    constexpr int WORKERS = 8;
    constexpr int ROUNDS = 60;
    std::atomic<int> wrong{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < WORKERS; ++worker) {
        workers.emplace_back([&, worker] {
            // Each thread converts a word unique to that thread, so a result delivered to the
            // wrong thread appears as an incorrect value rather than as an empty result.
            const std::string mine = std::to_string(worker);
            for (int round = 0; round < ROUNDS; ++round) {
                LinguistApi::LinguistConvertInput input;
                input.depth = LinguistApi::Depth::Onsets;
                input.words.push_back({mine, std::nullopt, std::nullopt});
                auto result = session.convert(singer, "zxx", input);
                if (!result || (*result)->words.size() != 1u ||
                    (*result)->words[0].pronunciation != mine) {
                    ++wrong;
                }
            }
        });
    }
    for (auto &thread : workers) {
        thread.join();
    }
    BOOST_CHECK_EQUAL(wrong.load(), 0);
}

/// probe() must remain responsive while conversions are running: a host calls it to draw its user
/// interface, and a pool that made drawing wait for a model to open would be worse than no pool.
BOOST_AUTO_TEST_CASE(test_LinguistSession_StaysAnswerableWhileConverting) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    std::atomic<bool> stop{false};
    std::thread worker([&] {
        while (!stop.load()) {
            (void) session.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets));
        }
    });

    for (int i = 0; i < 2000; ++i) {
        const auto status = session.probe(singer, "zxx");
        BOOST_REQUIRE(status.readiness != wolf::Readiness::Unavailable);
        BOOST_REQUIRE(session.catalog()->singers().size() == 1u);
    }
    stop = true;
    worker.join();
}

/// A ContribLocator carries no version, so two loaded versions of one voicebank make a request
/// that specifies only the locator ambiguous. Selecting one version would route to a version that
/// no caller requested, so the request is rejected with a reason distinct from "not found".
BOOST_AUTO_TEST_CASE(test_LinguistSession_RefusesAnAmbiguousSinger) {
    srt::SynthUnit unit;
    configure(unit);
    auto first = load(unit, "singer-zxx");
    auto second = load(unit, "singer-zxx-v2");
    BOOST_REQUIRE(first.version() != second.version());

    wolf::LinguistSession session(unit);
    BOOST_REQUIRE_EQUAL(session.catalog()->singers().size(), 2u);

    wolf::SingerRef unversioned;
    unversioned.locator = srt::ContribLocator(first.id(), "singer", "s");

    const auto status = session.probe(unversioned, "zxx");
    BOOST_CHECK(status.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(status.reason.find("several") != std::string::npos,
                        "the reason should say which of the two problems it is: " + status.reason);
    BOOST_CHECK(!session.convert(unversioned, "zxx", Line({"1"}, LinguistApi::Depth::Onsets)));

    // With the version specified, each voicebank version works and has a separate pool entry.
    for (const auto &package : {first, second}) {
        const auto singer = refOf(package, "s");
        BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
        auto result = session.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets));
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "1");
    }

    // Releasing one version leaves the other warm, which is the benefit of keying on the version.
    session.release(refOf(first, "s"));
    BOOST_CHECK(session.probe(refOf(first, "s"), "zxx").readiness == wolf::Readiness::Cold);
    BOOST_CHECK(session.probe(refOf(second, "s"), "zxx").readiness == wolf::Readiness::Ready);
}

/// Two sessions on one unit are safe, and neither interferes with the other.
///
/// The L6 design recorded "one session per unit" as a simplification, but no code enforced or
/// checked the restriction, so a host that created a second session would discover the problem
/// only at run time. The restriction has no basis: sessions share no mutable state except the unit
/// itself, which the framework locks, and two process-wide singletons with their own locks. Each
/// session builds its own pipeline, its own pool and its own caches.
BOOST_AUTO_TEST_CASE(test_LinguistSession_TwoSessionsShareOneUnit) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");
    const auto singer = refOf(package, "s");

    wolf::LinguistSession first(unit);
    wolf::LinguistSession second(unit);

    BOOST_REQUIRE_EQUAL(first.catalog()->singers().size(), 1u);
    BOOST_REQUIRE_EQUAL(second.catalog()->singers().size(), 1u);

    // Warming one session must not make the other report Ready: the pools are separate.
    BOOST_CHECK(first.warm(singer, "zxx"));
    BOOST_CHECK(first.probe(singer, "zxx").readiness == wolf::Readiness::Ready);
    BOOST_CHECK(second.probe(singer, "zxx").readiness == wolf::Readiness::Cold);

    // A release in one session must not affect the other.
    first.release(singer);
    BOOST_CHECK(second.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets)));
    BOOST_CHECK(first.convert(singer, "zxx", Line({"2"}, LinguistApi::Depth::Onsets)));

    constexpr int ROUNDS = 40;
    std::atomic<int> wrong{0};
    const auto churn = [&](wolf::LinguistSession &session, const std::string &word) {
        for (int round = 0; round < ROUNDS; ++round) {
            LinguistApi::LinguistConvertInput input;
            input.depth = LinguistApi::Depth::Onsets;
            input.words.push_back({word, std::nullopt, std::nullopt});
            auto result = session.convert(singer, "zxx", input);
            if (!result || (*result)->words.size() != 1u ||
                (*result)->words[0].pronunciation != word) {
                ++wrong;
            }
        }
    };
    std::thread one(churn, std::ref(first), "3");
    std::thread two(churn, std::ref(second), "4");
    one.join();
    two.join();
    BOOST_CHECK_EQUAL(wrong.load(), 0);
}

/// A composition without a phoneme member is valid, and the session reports its maximum depth.
///
/// The ecosystem already uses this combination for the syllable languages: the language package
/// supplies the G2P and a voicebank supplies the phoneme stage.
///
/// Regression: a linguist without linguist/s2p must load and report Pronunciation, instead of
/// requiring the whole composition to be placed in the voicebank.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReportsHowDeepALanguageGoes) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-shallow");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    const auto status = session.probe(singer, "nld");
    BOOST_CHECK(status.readiness == wolf::Readiness::Cold);
    BOOST_CHECK(status.maxDepth == LinguistApi::Depth::Pronunciation);

    // A request deeper than the composition supports returns the available layers rather than an
    // error or a silent gap.
    auto result = session.convert(singer, "nld", Line({"x"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "a b");
    BOOST_CHECK((*result)->words[0].phonemes.empty());
    BOOST_CHECK((*result)->words[0].onsets.empty());
}

/// Coverage is reported without a verdict, except if no phoneme is covered.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReportsPhonemeCoverage) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-shallow");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // No phoneme set has been supplied for the singer, which differs from an empty set.
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Unknown);

    // Two of the four declared phonemes are covered, and the language declares its list complete.
    session.setSingerPhonemes(singer, {"a", "b", "z"});
    const auto partial = session.probe(singer, "nld");
    BOOST_CHECK(partial.coverageKind == wolf::LanguageStatus::CoverageKind::Exact);
    BOOST_CHECK_CLOSE(partial.coverage, 0.5, 1e-6);
    BOOST_CHECK(partial.missingPhonemes == std::vector<std::string>({"c", "d"}));
    // Partial coverage does not make the route unusable.
    BOOST_CHECK(partial.readiness == wolf::Readiness::Cold);

    // Zero coverage of a complete inventory, however, indicates an incorrect route.
    session.setSingerPhonemes(singer, {"z"});
    const auto none = session.probe(singer, "nld");
    BOOST_CHECK(none.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(none.reason.find("none of the") != std::string::npos, none.reason);
}

/// A phoneme table for a singer absent from the catalog is ignored rather than stored, and a table
/// survives a refresh only while its singer remains loaded.
///
/// Regression: a table for a singer that unloaded must be reclaimed, and a voicebank reinstalled
/// after an unload must not report the coverage of the previous installation.
BOOST_AUTO_TEST_CASE(test_LinguistSession_PhonemeTablesFollowTheCatalog) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-shallow");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // The singer is not loaded, so the table is ignored and the loaded singer is not affected.
    session.setSingerPhonemes(refOf(package, "nobody"), {"a"});
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Unknown);

    session.setSingerPhonemes(singer, {"a", "b", "z"});
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Exact);

    // A refresh that still finds the singer keeps the table supplied by the host.
    session.refresh();
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Exact);

    // After an unload and a refresh, the table is discarded with the singer; after a reload, no
    // table has been supplied for the new installation.
    package.reset();
    session.refresh();
    BOOST_CHECK(session.catalog()->find(singer) == nullptr);
    package = load(unit, "singer-shallow");
    session.refresh();
    BOOST_REQUIRE(session.catalog()->find(singer) != nullptr);
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Unknown);
}

/// Checks a singer whose languages all route to modules that are not linguist contributions,
/// loaded in a transaction that contains no linguist contribution.
///
/// The loader invokes only the validators of providers already created, and nothing in this
/// transaction creates the wolf provider, so the language map is not checked and the package
/// loads. The same map loaded together with a language package is rejected (singer-wrong-target).
/// wolf cannot reject the package at this point; instead, it reports why the singer has no
/// languages rather than reporting that the singer does not exist.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ExplainsASingerWhoseLanguagesWereNotMounted) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-inference-route");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // The singer is not a linguist domain object and is therefore absent from the catalog.
    BOOST_CHECK(session.catalog()->find(singer) == nullptr);

    const auto status = session.probe(singer, "zxx");
    BOOST_CHECK(status.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(status.reason.find("no such singer") == std::string::npos, status.reason);
    BOOST_CHECK_MESSAGE(status.reason.find("lang/model") != std::string::npos, status.reason);
    BOOST_CHECK_MESSAGE(status.reason.find("not a linguist contribution") != std::string::npos,
                        status.reason);

    const auto warmed = session.warm(singer, "zxx");
    BOOST_REQUIRE(!warmed);
    BOOST_CHECK_MESSAGE(warmed.error().message().find("lang/model") != std::string::npos,
                        warmed.error().message());

    // A language that the singer does not declare is still reported as undeclared.
    const auto other = session.probe(singer, "cmn");
    BOOST_CHECK(other.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(other.reason.find("does not declare cmn") != std::string::npos,
                        other.reason);
}

/// A conversion whose executive fails the whole batch is a property of the route rather than of
/// the words: the failure is cached like a failure of executive creation, probe() reports the
/// language Unavailable, and only refresh() clears the verdict.
///
/// Regression: the failure of executive->start() was returned to the caller and then forgotten, so
/// probe() reported Cold and later conversions paid for a batch that cannot succeed. The
/// miscounting g2p of the fixture is a fault that fails the batch as a whole.
BOOST_AUTO_TEST_CASE(test_LinguistSession_RemembersABatchFailureUntilRefresh) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-miscount");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    auto result = session.convert(singer, "nld", Line({"one", "two"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE_MESSAGE(!result, "the miscounting g2p should have failed the batch");
    // The error carries the code and the text of the failing stage, which is the fault that the
    // readiness query replays.
    BOOST_CHECK(result.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK_MESSAGE(result.error().message().find("g2p") != std::string::npos,
                        result.error().message());

    const auto status = session.probe(singer, "nld");
    BOOST_CHECK(status.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(status.reason.find("g2p") != std::string::npos, status.reason);

    // A release frees the resources of the singer and does not repair a failed route, so the
    // verdict survives it.
    session.release(singer);
    BOOST_CHECK(session.probe(singer, "nld").readiness == wolf::Readiness::Unavailable);

    // A refresh is the release point of the failure cache: the language is unknown again rather
    // than reported as broken.
    session.refresh();
    const auto rescued = session.probe(singer, "nld");
    BOOST_CHECK_MESSAGE(rescued.readiness == wolf::Readiness::Cold, rescued.reason);
    BOOST_CHECK(rescued.reason.empty());
}

/// A per-word failure is a result of the conversion and not a failed route: the batch ran, the
/// words that no stage produced carry their own error, and the language must stay usable.
///
/// This is the counter-example to the case above: the same session path, with the failure reported
/// per word by the chain instead of for the whole batch, must leave nothing in the failure cache.
BOOST_AUTO_TEST_CASE(test_LinguistSession_KeepsAPerWordFailureOutOfTheCache) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-chain");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // The dictionary of the chain of this fixture covers "hello"; no step covers "unlisted" and
    // that chain has no fallback step, so the word-level error is what the caller receives.
    auto result =
        session.convert(singer, "deu", Line({"hello", "unlisted"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE_MESSAGE(result, wolf::test::why(result));
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "hh ax l ow");
    BOOST_CHECK((*result)->words[1].error == wolf::Api::G2P::L1::Error::PhonemeGenerationFailed);

    // The batch ran to completion, so the executive is pooled again: the route is warm, not broken.
    const auto status = session.probe(singer, "deu");
    BOOST_CHECK_MESSAGE(status.readiness == wolf::Readiness::Ready, status.reason);
    BOOST_CHECK(status.reason.empty());
}

/// A cancelled conversion is not a failed route either: the executive reports the cancellation
/// through its state and returns the part of the batch that it finished, so the session caches
/// nothing and keeps serving the language.
///
/// The runaway fixture is the only one whose batch runs long enough to be cancelled while it is
/// running. It needs the scripted inference variants, and a build without them cannot load it, so
/// the case skips instead of reporting a pass.
BOOST_AUTO_TEST_CASE(test_LinguistSession_KeepsACancelledConversionOutOfTheCache) {
    srt::SynthUnit unit;
    configure(unit);
    auto opened =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-runaway", srt::SynthUnit::Load);
    if (!opened) {
        wolf::test::skip("the runaway fixture needs the scripted inference variants: " +
                         opened.error().toString());
    }
    auto package = opened.take();

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // The scripted stage is created only by a conversion that reaches the phoneme layer, so a batch
    // that stops at the pronunciation layer would not tell whether this fixture can run at all.
    auto runnable = session.convert(singer, "nld", Line({"ok"}, LinguistApi::Depth::Phonemes));
    if (!runnable) {
        wolf::test::skip("the runaway fixture cannot reach its scripted stage: " +
                         runnable.error().toString());
    }
    BOOST_REQUIRE(session.probe(singer, "nld").readiness == wolf::Readiness::Ready);

    wolf::CancelToken token;
    std::thread canceller([&token] {
        // Long enough for the scripted stage to be running, as the pipe-chain case assumes.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });
    const Line line({"loop"}, LinguistApi::Depth::Phonemes);
    auto result = session.convert(singer, "nld", line, token);
    canceller.join();

    // A cancellation is a result rather than an error: the token is what distinguishes it from a
    // conversion with empty output.
    BOOST_REQUIRE_MESSAGE(result, wolf::test::why(result));
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
    BOOST_CHECK(token.cancelled());

    // The cancellation is not a failed route: the language is still as ready as it was before the
    // cancelled batch ran.
    const auto status = session.probe(singer, "nld");
    BOOST_CHECK_MESSAGE(status.readiness == wolf::Readiness::Ready, status.reason);
    BOOST_CHECK(status.reason.empty());

    // The language remains usable: the cancelled executive is not lent again, but the session
    // builds another one instead of reporting a broken route.
    BOOST_CHECK(session.convert(singer, "nld", Line({"x"}, LinguistApi::Depth::Phonemes)));
}

/// A cached failure is replayed with the kind of its error code and with the whole text of the
/// error, not with the message alone.
///
/// Regression: the reason used to be the message of the cached error, so a host received "the g2p
/// stage returned 1 entries for 2 inputs" without the code that says what kind of fault it is. The
/// kind is the canned text of the code, which synthrt fixes, and the case compares against that
/// text as a prefix rather than against a substring that every message happens to contain.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReplaysACachedFailureWithItsCodeKind) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-miscount");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    auto result = session.convert(singer, "nld", Line({"one", "two"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE_MESSAGE(!result, "the miscounting g2p should have failed the batch");
    const auto message = result.error().message();

    const auto status = session.probe(singer, "nld");
    BOOST_REQUIRE_MESSAGE(status.readiness == wolf::Readiness::Unavailable, status.reason);

    // The kind of the code, read from the code itself, followed by the text of the failure. The
    // message of this error says nothing about the kind, so only the replay can supply it.
    const auto kind = srt::Error(srt::Error::InvalidFormat).code().message();
    BOOST_REQUIRE_MESSAGE(!kind.empty(), "the code of the failure should have a canned text");
    const auto prefix = kind + ": ";
    BOOST_CHECK_MESSAGE(status.reason.compare(0, prefix.size(), prefix) == 0, status.reason);
    BOOST_CHECK_MESSAGE(status.reason.find(message) != std::string::npos, status.reason);
    // The kind is not repeated: a message that already is the canned text of its code is replayed
    // without the prefix, so that a failure recorded without text does not read as "file not
    // found: file not found".
    BOOST_CHECK_MESSAGE(status.reason.find(kind + ": " + kind) == std::string::npos, status.reason);
}

BOOST_AUTO_TEST_SUITE_END()
