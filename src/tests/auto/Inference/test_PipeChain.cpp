#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <vector>

#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Session/LinguistSession.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace LinguistApi = wolf::Api::Linguist::L1;

namespace {

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit, {wolf::test::fixtureRoot()});
    }

    std::vector<LinguistApi::LinguistWordOutput> convert(srt::SynthUnit &unit,
                                                         const std::vector<std::string> &lyrics,
                                                         const std::string &language = "eng") {
        auto handle = unit.openPackage(wolf::test::fixtureRoot() / "singer-chain",
                                       srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the singer should have loaded: " + handle.error().toString());
        }
        auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
            *handle->contribution("singer", "s")->as<srt::SingerSpec>());
        BOOST_REQUIRE(base != nullptr);
        auto *extension = base->as<LinguistApi::WolfPipelineExtension>();

        LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
        auto pipeline = extension->createPipeline(pipelineOptions);
        BOOST_REQUIRE(pipeline);

        LinguistApi::LinguistRuntimeOptions options;
        auto linguist = (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist(
            language, options);
        BOOST_REQUIRE(linguist);

        LinguistApi::LinguistConvertInput input;
        input.depth = LinguistApi::Depth::Pronunciation;
        for (const auto &lyric : lyrics) {
            input.words.push_back({lyric, std::nullopt, std::nullopt});
        }
        auto result = (*linguist)->start(input);
        if (!result) {
            BOOST_FAIL("the conversion should have run: " + result.error().toString());
        }
        return (*result)->words;
    }

    /// Loads a package at \a root whose only dictionary is \a dictionary, and returns the load
    /// error text, or an empty string if the package loaded, which indicates that the format under
    /// test was accepted.
    std::string dictRefusal(const fs::path &root, const std::string &dictionary) {
        fs::remove_all(root);
        fs::create_directories(root / "inferences" / "g2p");
        std::ofstream(root / "inferences" / "g2p" / "dict.txt") << dictionary;
        std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-chain-dict",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": { "inference": [ { "id": "g2p", "path": "./inferences/g2p/inference.json" } ] }
})";
        std::ofstream(root / "inferences" / "g2p" / "inference.json") << R"({
    "interface": "org.openvpi.wolf.inference.G2P",
    "level": 1,
    "variant": "pipe-chain",
    "configuration": {
        "formatVersion": 1,
        "steps": [ { "step": "dict", "params": { "file": "./dict.txt" } } ]
    }
})";

        srt::SynthUnit unit;
        unit.setPluginPaths("inference", {fs::path(WOLF_TEST_INFERENCE_PLUGIN_DIR)});
        auto handle = unit.openPackage(root, srt::SynthUnit::Load);
        std::string refusal;
        if (!handle) {
            refusal = handle.error().toString();
        }
        fs::remove_all(root);
        return refusal;
    }

}

BOOST_AUTO_TEST_SUITE(test_PipeChain)

/// The chain in the fixture consists of dictionary, cleanup and dictionary steps, the structure
/// used by the shipped English package. A word that only the second lookup can find proves that
/// the later step reads the cleanup result and that the two lookups do not conflict on one word.
BOOST_AUTO_TEST_CASE(test_PipeChain_LooksUpAgainAfterCleaning) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "HELLO", "read"});
    BOOST_REQUIRE_EQUAL(words.size(), 3u);

    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");
    // Found only after the cleanup step converted the word to lower case.
    BOOST_CHECK_EQUAL(words[1].pronunciation, "hh ax l ow");
    BOOST_CHECK_EQUAL(words[2].pronunciation, "r iy d");
}

/// A word listed more than once in a dictionary is one word with several readings, and an entry
/// numbered by the CMU convention for additional readings belongs to the same word rather than
/// forming a separate key.
BOOST_AUTO_TEST_CASE(test_PipeChain_MergesRepeatedDictionaryEntries) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 1u);
    BOOST_REQUIRE_EQUAL(words[0].candidates.size(), 2u);
    // Candidates follow file order, so the unnumbered entry is first and supplies the
    // pronunciation.
    BOOST_CHECK_EQUAL(words[0].candidates[0], "hh ax l ow");
    BOOST_CHECK_EQUAL(words[0].candidates[1], "hh eh l ow");
}

/// A word for which no step produced a pronunciation is returned with an error, not as an empty
/// success.
///
/// This case covers one of two routes to that result: the eng chain ends in a fallback that
/// produces nothing (useOriginal false, empty defaultPronunciation). The other route, a chain
/// without a fallback step, is covered by ReportsUnproducedWordsWithoutAFallback below. Both
/// routes must report the same result, because reporting success with an empty pronunciation
/// would violate the rule that the pronunciation of a converted word is its first candidate.
BOOST_AUTO_TEST_CASE(test_PipeChain_ReportsWhenTheFallbackHasNothingToGive) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"unlisted", "-"});
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::PhonemeGenerationFailed);
    BOOST_CHECK(words[0].pronunciation.empty());
    BOOST_CHECK(words[0].candidates.empty());

    // Classified as Copy, so no producing step processed the word and it is returned unchanged.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK(words[1].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[1].pronunciation, "-");
}

/// A converted word carries its pronunciation as its first candidate. The dictionary step already
/// satisfies this rule; the case pins it because the candidate list depends on the rule: a caller
/// that offers alternatives shows the current pronunciation first.
BOOST_AUTO_TEST_CASE(test_PipeChain_LeadsCandidatesWithThePronunciation) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 1u);
    BOOST_REQUIRE(!words[0].candidates.empty());
    BOOST_CHECK_EQUAL(words[0].candidates.front(), words[0].pronunciation);
}

/// Covers the other route to the same result: the deu chain has no fallback step, so the words
/// missing from its dictionaries reach the end of the chain unchanged. The corresponding case
/// above covers the first route.
BOOST_AUTO_TEST_CASE(test_PipeChain_ReportsUnproducedWordsWithoutAFallback) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "unlisted"}, "deu");
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    // The dictionary covered the first word.
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");

    // No step covered the second word, and the chain has no fallback step.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[1].error == wolf::Api::G2P::L1::Error::PhonemeGenerationFailed);
    BOOST_CHECK(words[1].pronunciation.empty());
    BOOST_CHECK(words[1].candidates.empty());
}

/// Classification must precede the steps that it governs. A verify step after a producing step
/// would reclassify words that already have a pronunciation, and a Copy mark at that point would
/// silently discard a dictionary hit.
BOOST_AUTO_TEST_CASE(test_PipeChain_RefusesClassifyingAfterProducing) {
    const auto root = fs::temp_directory_path() / "wolf-chain-lateverify";
    fs::remove_all(root);
    fs::create_directories(root / "inferences" / "g2p");
    std::ofstream(root / "inferences" / "g2p" / "dict.txt") << "a\tp q\n";
    std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-chain-lateverify",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": { "inference": [ { "id": "g2p", "path": "./inferences/g2p/inference.json" } ] }
})";
    std::ofstream(root / "inferences" / "g2p" / "inference.json") << R"({
    "interface": "org.openvpi.wolf.inference.G2P",
    "level": 1,
    "variant": "pipe-chain",
    "configuration": {
        "formatVersion": 1,
        "steps": [
            { "step": "dict", "params": { "file": "./dict.txt" } },
            { "step": "verify", "params": { "entries": [
                { "type": "array", "value": ["a"], "mode": "copy" } ] } }
        ]
    }
})";

    srt::SynthUnit unit;
    std::vector<fs::path> inference = {fs::path(WOLF_TEST_INFERENCE_PLUGIN_DIR)};
    unit.setPluginPaths("inference", inference);
    auto handle = unit.openPackage(root, srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!handle, "a late verify step should have been rejected");
    BOOST_CHECK(handle.error().toString().find("must precede") != std::string::npos);
    fs::remove_all(root);
}

/// A dictionary line contains one word and one pronunciation. A second tab is a format that this
/// generation does not define, and including it in the pronunciation would pass a tab to the
/// phoneme splitter, producing a corrupted reading that no downstream step would report.
BOOST_AUTO_TEST_CASE(test_PipeChain_RefusesDictionaryLinesItCannotRead) {
    const auto multi =
        dictRefusal(fs::temp_directory_path() / "wolf-chain-dict-multicol", "a\tp q\nb\tx y\tz\n");
    BOOST_CHECK_MESSAGE(multi.find("dict.txt line 2") != std::string::npos,
                        "the diagnosis should name the file and the line: " + multi);
    BOOST_CHECK_MESSAGE(multi.find("multiple tab separators") != std::string::npos, multi);

    const auto missing =
        dictRefusal(fs::temp_directory_path() / "wolf-chain-dict-notab", "a\tp q\nbroken\n");
    BOOST_CHECK_MESSAGE(missing.find("dict.txt line 2") != std::string::npos,
                        "the diagnosis should name the file and the line: " + missing);
    BOOST_CHECK_MESSAGE(missing.find("missing tab separator") != std::string::npos, missing);
}

/// A stop request to the linguist must reach the running stage. A stage receives the whole batch
/// in one call, so a request handled only at the linguist level would wait until that call
/// returns, and for a script or a model that wait is the one that requires interruption.
///
/// The language used by this case resides in a separate package because its phoneme stage is a
/// script, and a package containing a script cannot load in a build without LuaJIT. Keeping the
/// language in the main chain fixture caused every other case in this file to fail in such a
/// build.
BOOST_AUTO_TEST_CASE(test_PipeChain_StopReachesTheRunningStage) {
#ifndef WOLF_TEST_HAS_LUAJIT
    return;
#else
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-runaway", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);
    auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
        *handle->contribution("singer", "s")->as<srt::SingerSpec>());
    BOOST_REQUIRE(base != nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = base->as<LinguistApi::WolfPipelineExtension>()->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);
    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("nld", options);
    BOOST_REQUIRE(linguist);
    auto *executive = *linguist;

    std::chrono::steady_clock::time_point asked;
    // Waits until the execution is running before stopping it, rather than relying on a delay that
    // a slow machine could exceed: a stop that arrived first would be consumed by the start and
    // prove nothing. The subsequent short pause allows the script stage to start running. Whether
    // the stop then arrives inside the script or between two stages, the stop must be handled
    // promptly and leave the execution Canceled; both conditions are checked below.
    std::thread stopper([executive, &asked] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (executive->state() != srt::ITask::Running &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        asked = std::chrono::steady_clock::now();
        (void) executive->stop();
    });

    // The phoneme stage of this language is a script that does not return for this word.
    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Phonemes;
    input.words.push_back({"loop", std::nullopt, std::nullopt});
    auto result = executive->start(input);
    const auto answered = std::chrono::steady_clock::now();
    stopper.join();

    BOOST_REQUIRE(result);
    BOOST_CHECK(executive->state() == srt::ITask::Canceled);

    // Reported so that the runtime documentation can quote a measured figure rather than an
    // estimate. The bound is the instruction budget of the interrupt hook, not a timer, so the
    // latency scales with the cost of one instruction batch rather than with the input size.
    const auto milliseconds = std::chrono::duration<double, std::milli>(answered - asked).count();
    BOOST_TEST_MESSAGE("stop answered in " << milliseconds << " ms");
    BOOST_CHECK_MESSAGE(milliseconds < 1000.0, "a stop should be answered promptly, took " +
                                                   std::to_string(milliseconds) + " ms");
#endif
}

/// A cancel token set during a session conversion reaches the running stage without blocking any
/// other operation: cancel() calls stop() outside its lock, and the executive being stopped is not
/// returned to the pool until stop() has returned.
BOOST_AUTO_TEST_CASE(test_PipeChain_CancelTokenReachesTheRunningStage) {
#ifndef WOLF_TEST_HAS_LUAJIT
    return;
#else
    srt::SynthUnit unit;
    configure(unit);
    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-runaway", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);

    wolf::LinguistSession session(unit);
    const wolf::SingerRef singer{srt::ContribLocator(handle->id(), "singer", "s"),
                                 handle->version()};

    wolf::CancelToken token;
    std::thread canceller([&token] {
        // The delay is long enough for the script stage to be running.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Phonemes;
    input.words.push_back({"loop", std::nullopt, std::nullopt});
    const auto started = std::chrono::steady_clock::now();
    auto result = session.convert(singer, "nld", input, token);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    canceller.join();

    // The result is not an error: the words completed before the stop are returned converted, the
    // remaining words are returned unconverted, and the token indicates that the conversion was
    // cancelled.
    BOOST_REQUIRE_MESSAGE(result, wolf::test::why(result));
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
    BOOST_CHECK((*result)->words[0].phonemes.empty());
    BOOST_CHECK(token.cancelled());
    BOOST_CHECK_LT(std::chrono::duration<double>(elapsed).count(), 10.0);

    // The session is still usable for the same language afterwards.
    LinguistApi::LinguistConvertInput next;
    next.depth = LinguistApi::Depth::Pronunciation;
    next.words.push_back({"x", std::nullopt, std::nullopt});
    BOOST_CHECK(session.convert(singer, "nld", next));
#endif
}

/// The inference contract specifies an input ordering that every G2P module must follow: an empty
/// lyric is skipped, and a lyric containing whitespace around or inside a word is invalid input.
/// Regression: both cases must be reported as mode=skip, not as a conversion failure.
BOOST_AUTO_TEST_CASE(test_PipeChain_HonoursTheInputOrdering) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "", "   ", "two words", " hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 5u);

    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");

    // Empty and whitespace-only lyrics are both skipped without an error.
    for (const auto index : {1u, 2u}) {
        BOOST_CHECK(words[index].mode == wolf::Api::G2P::L1::Mode::Skip);
        BOOST_CHECK(words[index].error == wolf::Api::G2P::L1::Error::None);
        BOOST_CHECK(words[index].pronunciation.empty());
        BOOST_CHECK(words[index].candidates.empty());
    }

    // A word with whitespace inside or around it is passed through so that the host receives its
    // input unchanged, and the error indicates that the word was not converted. Leading
    // whitespace also counts, which distinguishes this case from a word that the chain could not
    // find.
    for (const auto index : {3u, 4u}) {
        BOOST_CHECK(words[index].error == wolf::Api::G2P::L1::Error::InvalidInput);
    }
    BOOST_CHECK_EQUAL(words[3].pronunciation, "two words");
    BOOST_CHECK_EQUAL(words[4].pronunciation, " hello");
}

BOOST_AUTO_TEST_SUITE_END()
