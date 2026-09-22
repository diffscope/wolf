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

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace LinguistApi = wolf::Api::Linguist::L1;

namespace {

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit, {fs::path(WOLF_TEST_FIXTURE_DIR)});
    }

    std::vector<LinguistApi::LinguistWordOutput> convert(srt::SynthUnit &unit,
                                                         const std::vector<std::string> &lyrics,
                                                         const std::string &language = "eng") {
        auto handle = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-chain",
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

    /// Loads a package whose only dictionary is the given text, and returns why it was refused.
    /// Empty when it loaded, which would mean the shape under test was accepted.
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

/// The chain in the fixture is dictionary, cleanup, dictionary — the shape the shipped English
/// package uses. A word only the second lookup can find proves the cleanup result is what the
/// later step reads, and that the two lookups do not fight over one word.
BOOST_AUTO_TEST_CASE(test_PipeChain_LooksUpAgainAfterCleaning) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "HELLO", "read"});
    BOOST_REQUIRE_EQUAL(words.size(), 3u);

    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");
    // Found only after the cleanup step lower cased it.
    BOOST_CHECK_EQUAL(words[1].pronunciation, "hh ax l ow");
    BOOST_CHECK_EQUAL(words[2].pronunciation, "r iy d");
}

/// A word written more than once in a dictionary is one word with several readings, and the CMU
/// convention of numbering the extra ones is that same word rather than a key of its own.
BOOST_AUTO_TEST_CASE(test_PipeChain_MergesRepeatedDictionaryEntries) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 1u);
    BOOST_REQUIRE_EQUAL(words[0].candidates.size(), 2u);
    // File order, so the plain entry leads and is what the pronunciation takes.
    BOOST_CHECK_EQUAL(words[0].candidates[0], "hh ax l ow");
    BOOST_CHECK_EQUAL(words[0].candidates[1], "hh eh l ow");
}

/// A word nothing produced comes back with an error, not with an empty success.
///
/// This is one of the two routes there: the eng chain ends in a fallback that has nothing to give
/// (useOriginal false, empty defaultPronunciation). The other route — a chain with no fallback step
/// at all — is ReportsUnproducedWordsWithoutAFallback below. Both have to say the same thing,
/// because reporting success with an empty pronunciation would break the rule that a converted
/// word's pronunciation is its first candidate.
BOOST_AUTO_TEST_CASE(test_PipeChain_ReportsWhenTheFallbackHasNothingToGive) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"unlisted", "-"});
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::PhonemeGenerationFailed);
    BOOST_CHECK(words[0].pronunciation.empty());
    BOOST_CHECK(words[0].candidates.empty());

    // Classified copy, so no producing step ever touched it and it comes back as itself.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK(words[1].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[1].pronunciation, "-");
}

/// A converted word carries its pronunciation as the first of its candidates. The dictionary
/// step already satisfies that; this pins it, because the rule is what makes candidates usable at
/// all — a caller offering alternatives shows the current one first.
BOOST_AUTO_TEST_CASE(test_PipeChain_LeadsCandidatesWithThePronunciation) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 1u);
    BOOST_REQUIRE(!words[0].candidates.empty());
    BOOST_CHECK_EQUAL(words[0].candidates.front(), words[0].pronunciation);
}

/// The other route to the same answer: the deu chain carries no fallback step at all, so the words
/// its dictionaries missed reach the end of the chain untouched. See the case above for the pair.
BOOST_AUTO_TEST_CASE(test_PipeChain_ReportsUnproducedWordsWithoutAFallback) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "unlisted"}, "deu");
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    // The dictionary covered the first one.
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");

    // Nothing covered the second, and there is no fallback step to fall back to.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[1].error == wolf::Api::G2P::L1::Error::PhonemeGenerationFailed);
    BOOST_CHECK(words[1].pronunciation.empty());
    BOOST_CHECK(words[1].candidates.empty());
}

/// Classification has to come before the steps it governs. A verify step after a producing one
/// would re-decide words that already have a pronunciation, and a copy mark there would throw a
/// dictionary hit away without saying so.
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
    BOOST_REQUIRE_MESSAGE(!handle, "a late verify step should have been refused");
    BOOST_CHECK(handle.error().toString().find("has to precede") != std::string::npos);
    fs::remove_all(root);
}

/// A dictionary line carries one word and one pronunciation. A second tab is a shape this
/// generation does not define, and folding it into the pronunciation would hand a tab to the
/// phoneme splitter — a corrupted reading that nothing downstream would report.
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

/// Stopping the linguist has to reach the stage that is running. A stage is handed the whole
/// batch in one call, so a request answered only at this level would wait for that call to return
/// on its own — which for a script or a model is exactly the wait worth interrupting.
///
/// The language it uses lives in a package of its own, because its phoneme stage is a script and
/// a package holding one cannot load at all in a build without LuaJIT. Keeping it inside the main
/// chain fixture took every other case in this file down with it in such a build.
BOOST_AUTO_TEST_CASE(test_PipeChain_StopReachesTheRunningStage) {
#ifndef WOLF_TEST_HAS_LUAJIT
    return;
#else
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-runaway", srt::SynthUnit::Load);
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
    // Waits until the execution is really under way before stopping it, rather than counting on a
    // delay a slow machine could outlast: a stop that arrived first would be consumed by the start
    // and prove nothing. The short pause after that gives the script stage time to be the thing
    // running; whether the stop then lands inside the script or between two stages, it has to be
    // answered promptly and leave the execution Canceled, and both are checked below.
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

    // The phoneme stage of this language is a script that never returns for this word.
    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Phonemes;
    input.words.push_back({"loop", std::nullopt, std::nullopt});
    auto result = executive->start(input);
    const auto answered = std::chrono::steady_clock::now();
    stopper.join();

    BOOST_REQUIRE(result);
    BOOST_CHECK(executive->state() == srt::ITask::Canceled);

    // Reported so the runtime document can quote a measured figure rather than a guess. The
    // bound is the interrupt hook's instruction budget, not a timer, so it scales with how much
    // work one batch of instructions is rather than with the size of the input.
    const auto milliseconds = std::chrono::duration<double, std::milli>(answered - asked).count();
    BOOST_TEST_MESSAGE("stop answered in " << milliseconds << " ms");
    BOOST_CHECK_MESSAGE(milliseconds < 1000.0, "a stop should be answered promptly, took " +
                                                   std::to_string(milliseconds) + " ms");
#endif
}

/// The inference contract states an input ordering that every G2P module owes, and that none of
/// them used to honour: an empty lyric is skipped, and a lyric carrying whitespace is invalid
/// input. mode=skip was an enum value nothing produced.
BOOST_AUTO_TEST_CASE(test_PipeChain_HonoursTheInputOrdering) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, {"hello", "", "   ", "two words", " hello"});
    BOOST_REQUIRE_EQUAL(words.size(), 5u);

    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "hh ax l ow");

    // Empty and whitespace-only both skip, with nothing to say about them.
    for (const auto index : {1u, 2u}) {
        BOOST_CHECK(words[index].mode == wolf::Api::G2P::L1::Mode::Skip);
        BOOST_CHECK(words[index].error == wolf::Api::G2P::L1::Error::None);
        BOOST_CHECK(words[index].pronunciation.empty());
        BOOST_CHECK(words[index].candidates.empty());
    }

    // Whitespace inside or around a word: it passes through so the host can see what it sent,
    // and the error says it was not converted. Leading whitespace counts, which is what makes
    // this different from a word the chain simply could not find.
    for (const auto index : {3u, 4u}) {
        BOOST_CHECK(words[index].error == wolf::Api::G2P::L1::Error::InvalidInput);
    }
    BOOST_CHECK_EQUAL(words[3].pronunciation, "two words");
    BOOST_CHECK_EQUAL(words[4].pronunciation, " hello");
}

BOOST_AUTO_TEST_SUITE_END()
