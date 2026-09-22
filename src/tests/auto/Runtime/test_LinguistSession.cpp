#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

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
                              {fs::path(WOLF_TEST_PACKAGE_DIR), fs::path(WOLF_TEST_FIXTURE_DIR)});
    }

    /// Loads the passthrough singer and returns a handle to it. The handle has to outlive every
    /// use: it is what keeps the package, and therefore the specs, alive.
    srt::PackageHandle load(srt::SynthUnit &unit, const char *name) {
        return wolf::test::load(unit, fs::path(WOLF_TEST_FIXTURE_DIR) / name);
    }

    wolf::SingerRef refOf(const srt::PackageHandle &package, const char *id) {
        return {srt::ContribLocator(package.id(), "singer", id), package.version()};
    }

    /// A conversion input is a task payload, which is neither copyable nor movable, so it is
    /// built in place and handed over as a reference.
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

/// The catalog answers what a host needs before it converts anything: which singers there are,
/// which languages each declares, and what each of those is bound to.
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

    // Round tripping a catalog entry has to find the same entry.
    BOOST_CHECK(catalog->find(singer.ref) == &singer);

    // A catalog is a snapshot: it stays what it was even after the session moves on.
    session.refresh();
    BOOST_CHECK_EQUAL(catalog->singers().size(), 1u);
}

/// Cold means there is a route and it has not been tried; Ready means the next conversion loads
/// nothing. Asking must never be what causes the loading.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ProbeDoesNotWarm) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    // Asked a hundred times, still cold: a host polling it every frame must not load a model.
    for (int i = 0; i < 100; ++i) {
        BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    }

    BOOST_CHECK(session.warm(singer, "zxx"));
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Ready);

    // A refresh drops the readiness cache with the catalog it belonged to.
    session.refresh();
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
}

/// A language the singer does not declare, and a singer that is not there, are both Unavailable
/// with something a host can show rather than a bare failure.
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

    // Neither of them may claim a depth. There is no value that means "reaches nothing", so an
    // unavailable status carries the shallowest rather than the deepest: a caller that reads it
    // without checking readiness first then asks for less than it could, instead of asking for
    // more than exists. Both of these used to come back claiming Onsets.
    BOOST_CHECK(status.maxDepth == LinguistApi::Depth::Pronunciation);
    BOOST_CHECK(missing.maxDepth == LinguistApi::Depth::Pronunciation);
}

/// An undeclared language is refused the same way by every entry point, and refused from the
/// catalog rather than by trying: a five hundred note phrase must not turn one refusal into five
/// hundred attempts, which is the cache lite keeps by hand.
///
/// This checks the cheap half — a fact the catalog already knows. The other half, a route that is
/// declared but fails to build, needs a resource that breaks only at executive creation; no
/// fixture here has one, so it is not claimed to be covered.
BOOST_AUTO_TEST_CASE(test_LinguistSession_RefusesAnUndeclaredLanguageEveryTime) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(!session.warm(singer, "cmn"));
    BOOST_CHECK(session.probe(singer, "cmn").readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK(!session.convert(singer, "cmn", Line({"1"}, LinguistApi::Depth::Onsets)));

    // Nothing was built along the way: the declared language is still Cold, so the refusals did
    // not quietly warm anything.
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
}

/// Conversion keeps the L4 shapes: same input, same per word output, cut at the same depths.
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

    // Converting is also warming: the executive it built is the one it kept.
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Ready);

    // Running again reuses that executive rather than building a second one, which is only
    // observable in that it still works after the first one was returned.
    auto again = session.convert(singer, "zxx", Line({"3"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(again);
    BOOST_CHECK_EQUAL((*again)->words[0].pronunciation, "3");
}

/// Reserved markers are answered by the session and never reach a language, so the session owes
/// their output shape at every depth. Without this an S2P dictionary is asked to look up SP.
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
    // The ordinary words around them kept their places.
    BOOST_CHECK_EQUAL(words[0].pronunciation, "1");
    BOOST_CHECK_EQUAL(words[3].pronunciation, "2");

    // Cut shorter, a marker stops where every other word stops.
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

/// A host using another notation replaces the set, and one that pins a pronunciation is stating
/// intent the session does not second guess.
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
    // SP is now an ordinary word, and the passthrough language treats it as one.
    BOOST_CHECK_EQUAL((*swapped)->words[1].pronunciation, "SP");

    session.setReservedMarkers({"SP", "AP"});
    LinguistApi::LinguistConvertInput pinned;
    pinned.depth = LinguistApi::Depth::Pronunciation;
    pinned.words.push_back({"SP", std::string("s p"), std::nullopt});
    auto result = session.convert(singer, "zxx", pinned);
    BOOST_REQUIRE(result);
    // The host wrote a pronunciation, so the marker table was never consulted.
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "s p");
}

/// A singer's own declaration of its markers (the singer category's reservedPhonemes) takes
/// precedence over the session wide set, and leaves the session wide set itself untouched for
/// every other singer. Nobody hands the set over: the session reads it from the declaration.
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
    // SP is no longer this singer's marker, so the passthrough language sees it as a word.
    BOOST_CHECK_EQUAL((*result)->words[1].pronunciation, "SP");
    BOOST_CHECK_EQUAL(session.reservedMarkers().size(), 2u);
}

/// A line that is nothing but markers never reaches a language at all, and still comes back whole.
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

/// Installing a package while the user is editing is ordinary, so a refresh has to be safe against
/// a conversion in flight. The pool moves to a new generation instead of emptying: what is idle
/// goes now, what is out on loan goes when it comes back.
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

    // Wait until the worker has actually converted something before refreshing under it. Without
    // this the case is a race it can lose: on a loaded machine the main thread finishes all two
    // hundred refreshes before the worker is scheduled once, and then it has measured nothing
    // while still reporting a pass — or, as happened here, a spurious failure.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (converted.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    BOOST_CHECK_MESSAGE(converted.load() > 0,
                        "the worker never converted anything to refresh under");

    for (int i = 0; i < 200; ++i) {
        session.refresh();
    }
    stop = true;
    worker.join();

    // It kept going across the refreshes rather than stopping at the first one.
    BOOST_CHECK_GT(converted.load(), 1);
    // And the session is still usable afterwards, which a double free would not leave it.
    BOOST_CHECK(session.convert(singer, "zxx", Line({"4"}, LinguistApi::Depth::Onsets)));
}

/// Releasing one singer drops its pool entry and its package handle and leaves everything else
/// alone. This is the release point the host needs before unloading a voicebank.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReleasesOneSinger) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    BOOST_CHECK(session.warm(singer, "zxx"));
    session.release(singer);
    BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
    // Still in the catalog: releasing frees resources, it does not unload the package.
    BOOST_CHECK(session.catalog()->find(singer) != nullptr);
    BOOST_CHECK(session.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets)));
}

/// A token set before the conversion starts stops it rather than letting it run.
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
    // The batch never ran, so every word comes back in its unconverted shape. Asserting this is
    // the point: an earlier version stopped the executive and then started it anyway, and the
    // executive cleared the request on entry, so a token cancelled before convert() did nothing
    // at all and the whole chain ran to onsets.
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);
    for (const auto &word : (*result)->words) {
        BOOST_CHECK(word.pronunciation.empty());
        BOOST_CHECK(word.phonemes.empty());
        BOOST_CHECK(word.onsets.empty());
    }
    // And the session is not left broken: an uncancelled conversion still works.
    BOOST_CHECK(session.convert(singer, "zxx", Line({"3"}, LinguistApi::Depth::Onsets)));
}

/// A marker note whose phoneme layer the user pinned keeps that layer.
///
/// The session answers reserved markers itself instead of sending them to a language, but pinning
/// outranks that: an earlier version tested only for a pinned pronunciation, so editing an SP's
/// phonemes by hand had the edit thrown away and replaced by the marker.
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

    // Without a pinned layer the marker is still answered by the session, as before.
    LinguistApi::LinguistConvertInput plain;
    plain.depth = LinguistApi::Depth::Onsets;
    plain.words.push_back({"SP", std::nullopt, std::nullopt});
    auto marker = session.convert(singer, "zxx", plain);
    BOOST_REQUIRE(marker);
    BOOST_CHECK((*marker)->words.front().phonemes == std::vector<std::string>({"SP"}));
}

/// The pool exists so several conversions can run at once, and an executive carries one at a
/// time. Many threads on one singer and one language is the case that has to work: each gets its
/// own executive, and returning one must not hand it to somebody still using it.
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
            // Each thread converts a word only it uses, so a result landing on the wrong thread
            // shows up as a wrong value rather than as nothing at all.
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

/// probe() must stay answerable while conversions are running: a host asks it to draw its UI, and
/// a pool that made drawing wait on a model opening would be worse than no pool.
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
/// that names only the locator ambiguous. Picking one would route to a version nobody asked for,
/// so it is refused — and refused with the reason, which is not the same as "not found".
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

    // Named with its version, each one works and they are separate pool entries.
    for (const auto &package : {first, second}) {
        const auto singer = refOf(package, "s");
        BOOST_CHECK(session.probe(singer, "zxx").readiness == wolf::Readiness::Cold);
        auto result = session.convert(singer, "zxx", Line({"1"}, LinguistApi::Depth::Onsets));
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "1");
    }

    // Releasing one leaves the other warmed, which is what keying on the version buys.
    session.release(refOf(first, "s"));
    BOOST_CHECK(session.probe(refOf(first, "s"), "zxx").readiness == wolf::Readiness::Cold);
    BOOST_CHECK(session.probe(refOf(second, "s"), "zxx").readiness == wolf::Readiness::Ready);
}

/// Two sessions on one unit are safe, and neither disturbs the other.
///
/// The L6 design wrote down "one session per unit" as a simplification, and nothing ever enforced
/// or checked it — the worst of both, since a host that made a second session would find out at
/// run time. There is no reason for the restriction: a session shares nothing mutable with another
/// beyond the unit itself, which the framework locks, and two process wide singletons that lock
/// themselves. Each session builds its own pipeline, its own pool and its own caches.
BOOST_AUTO_TEST_CASE(test_LinguistSession_TwoSessionsShareOneUnit) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-zxx");
    const auto singer = refOf(package, "s");

    wolf::LinguistSession first(unit);
    wolf::LinguistSession second(unit);

    BOOST_REQUIRE_EQUAL(first.catalog()->singers().size(), 1u);
    BOOST_REQUIRE_EQUAL(second.catalog()->singers().size(), 1u);

    // Warming one must not make the other claim to be warm: the pools are separate.
    BOOST_CHECK(first.warm(singer, "zxx"));
    BOOST_CHECK(first.probe(singer, "zxx").readiness == wolf::Readiness::Ready);
    BOOST_CHECK(second.probe(singer, "zxx").readiness == wolf::Readiness::Cold);

    // And releasing in one must not disturb the other.
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

/// A composition without a phoneme member is a legal shape, and the session says how deep it goes.
///
/// This is the combination the ecosystem already uses for the syllable languages: the language
/// package supplies the G2P and a voicebank supplies the phoneme stage. Until now a linguist
/// without linguist/s2p failed to load outright, so the shape could only exist by putting the
/// whole composition in the voicebank.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReportsHowDeepALanguageGoes) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-shallow");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    const auto status = session.probe(singer, "nld");
    BOOST_CHECK(status.readiness == wolf::Readiness::Cold);
    BOOST_CHECK(status.maxDepth == LinguistApi::Depth::Pronunciation);

    // Asking for more than it has returns what it has, rather than an error or a silent gap.
    auto result = session.convert(singer, "nld", Line({"x"}, LinguistApi::Depth::Onsets));
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "a b");
    BOOST_CHECK((*result)->words[0].phonemes.empty());
    BOOST_CHECK((*result)->words[0].onsets.empty());
}

/// Coverage is reported, not judged — except when it is total.
BOOST_AUTO_TEST_CASE(test_LinguistSession_ReportsPhonemeCoverage) {
    srt::SynthUnit unit;
    configure(unit);
    auto package = load(unit, "singer-shallow");

    wolf::LinguistSession session(unit);
    const auto singer = refOf(package, "s");

    // Nobody has said what the singer can sing, which is not the same answer as "nothing".
    BOOST_CHECK(session.probe(singer, "nld").coverageKind ==
                wolf::LanguageStatus::CoverageKind::Unknown);

    // Half of the four declared phonemes, and the language says its list is the whole of it.
    session.setSingerPhonemes(singer, {"a", "b", "z"});
    const auto partial = session.probe(singer, "nld");
    BOOST_CHECK(partial.coverageKind == wolf::LanguageStatus::CoverageKind::Exact);
    BOOST_CHECK_CLOSE(partial.coverage, 0.5, 1e-6);
    BOOST_CHECK(partial.missingPhonemes == std::vector<std::string>({"c", "d"}));
    // Half is not a verdict the session makes: the route is still usable.
    BOOST_CHECK(partial.readiness == wolf::Readiness::Cold);

    // None of them is, though: a complete inventory covered not at all is a wrong route.
    session.setSingerPhonemes(singer, {"z"});
    const auto none = session.probe(singer, "nld");
    BOOST_CHECK(none.readiness == wolf::Readiness::Unavailable);
    BOOST_CHECK_MESSAGE(none.reason.find("none of the") != std::string::npos, none.reason);
}

BOOST_AUTO_TEST_SUITE_END()
