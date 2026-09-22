#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;
namespace LinguistApi = wolf::Api::Linguist::L1;

namespace {

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit, {fs::path(WOLF_TEST_FIXTURE_DIR)});
    }

    LinguistApi::WolfPipelineExtension *extensionOf(srt::ContribSpec *singer) {
        auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
            *singer->as<srt::SingerSpec>());
        return base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
    }

    /// Runs one batch of lyrics through the language a singer declares.
    std::vector<LinguistApi::LinguistWordOutput> convert(srt::SynthUnit &unit,
                                                         const std::string &singerPackage,
                                                         const std::string &language,
                                                         const std::vector<std::string> &lyrics) {
        auto handle =
            unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / singerPackage, srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL(singerPackage + " should have loaded: " + handle.error().toString());
        }
        auto *extension = extensionOf(handle->contribution("singer", "s"));
        BOOST_REQUIRE(extension != nullptr);

        LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
        auto pipeline = extension->createPipeline(pipelineOptions);
        BOOST_REQUIRE(pipeline);

        LinguistApi::LinguistRuntimeOptions options;
        auto linguist = (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist(
            language, options);
        if (!linguist) {
            BOOST_FAIL("the linguist should have been created: " + linguist.error().toString());
        }

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

}

BOOST_AUTO_TEST_SUITE(test_PinyinBackend)

/// The whole point of the shared backend: Mandarin and Cantonese resolve through one dictionary
/// root, so both work in one process. Their engines used to arrive with a dictionary tree each,
/// and the second one loaded silently disabled the first.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_ServesBothLanguagesAtOnce) {
    srt::SynthUnit unit;
    configure(unit);

    const auto mandarin = convert(unit, "singer-zh", "cmn", {"\xE4\xB8\xAD", "\xE5\x9B\xBD"});
    BOOST_REQUIRE_EQUAL(mandarin.size(), 2u);
    BOOST_CHECK_EQUAL(mandarin[0].pronunciation, "zhong");
    BOOST_CHECK_EQUAL(mandarin[1].pronunciation, "guo");

    // The same unit, the same process, the other engine.
    const auto cantonese = convert(unit, "singer-zh", "yue", {"\xE4\xB8\xAD", "\xE5\x9B\xBD"});
    BOOST_REQUIRE_EQUAL(cantonese.size(), 2u);
    BOOST_CHECK_EQUAL(cantonese[0].pronunciation, "zung");
    BOOST_CHECK_EQUAL(cantonese[1].pronunciation, "gwok");
}

/// The two languages of one voicebank must warm at the same time, not one after the other.
///
/// Building an engine reads cpp-pinyin's dictionaries, and the arbiter used to hold its process
/// wide lock across the whole construction, so every engine in the process was built in single
/// file. The lock now covers only the one publication of the global dictionary path; construction
/// runs outside it. This case exists to keep it that way and, under ThreadSanitizer, to say so
/// about the upstream constructor too.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_BuildsEnginesConcurrently) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-zh", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("singer-zh should have loaded: " + handle.error().toString());
    }
    auto *extension = extensionOf(handle->contribution("singer", "s"));
    BOOST_REQUIRE(extension != nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);
    auto *wolfPipeline = (*pipeline)->as<LinguistApi::WolfPipelineExecutive>();

    constexpr int ROUNDS = 8;
    std::atomic<int> wrong{0};
    const auto run = [&](const char *language, const char *expected) {
        for (int round = 0; round < ROUNDS; ++round) {
            LinguistApi::LinguistRuntimeOptions options;
            auto linguist = wolfPipeline->createLinguist(language, options);
            if (!linguist) {
                ++wrong;
                return;
            }
            LinguistApi::LinguistConvertInput input;
            input.depth = LinguistApi::Depth::Pronunciation;
            input.words.push_back({"\xE4\xB8\xAD", std::nullopt, std::nullopt});
            auto result = (*linguist)->start(input);
            if (!result || (*result)->words.size() != 1u ||
                (*result)->words[0].pronunciation != expected) {
                ++wrong;
            }
            // Deleting is what detaches a child from its pipeline; leaving them accumulates.
            delete *linguist;
        }
    };

    std::thread mandarin(run, "cmn", "zhong");
    std::thread cantonese(run, "yue", "zung");
    mandarin.join();
    cantonese.join();

    BOOST_CHECK_EQUAL(wrong.load(), 0);
}

/// A word's neighbours decide its reading, so the chain hands the backend runs of adjacent words
/// rather than one flat list of everything left to convert.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_KeepsPhraseContext) {
    srt::SynthUnit unit;
    configure(unit);

    // On its own the polyphonic character takes its first reading.
    const auto alone = convert(unit, "singer-zh", "cmn", {"\xE8\xA1\x8C"});
    BOOST_CHECK_EQUAL(alone[0].pronunciation, "xing");

    // Beside its phrase it takes the other one.
    const auto phrase = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(phrase.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[0].pronunciation, "yin");
    BOOST_CHECK_EQUAL(phrase[1].pronunciation, "hang");

    // A word the chain classified copy sits between them and breaks the run, so the phrase is no
    // longer a phrase. Without run splitting the two characters would look adjacent to the engine.
    const auto broken =
        convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "zhong", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(broken.size(), 3u);
    BOOST_CHECK(broken[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(broken[2].pronunciation, "xing");
}

/// The same rule holds when a host binds the backend directly instead of going through the chain.
///
/// The chain splits its own batches into runs of convertible words before calling anything, so it
/// never exposed this; the backend still has to do it itself, because a host that binds the G2P
/// contract directly hands over whatever the song has, gaps included. Flattening the batch made the
/// characters either side of a skipped word adjacent, so they read as a phrase the song never had.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_BreaksRunsWhenCalledDirectly) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the engine package should have loaded: " + handle.error().toString());
    }
    auto *spec = handle->contribution("inference", "pinyin")->as<srt::InferenceSpec>();
    BOOST_REQUIRE(spec != nullptr);

    // One executive per call, and no attempt to re-arm it afterwards: on an
    // Expected<unique_ptr<...>> the arrow reaches the stored pointer, so a reset() there destroys
    // the executive instead of preparing it for another batch.
    const auto convertWords = [&](const std::vector<std::string> &lyrics) {
        G2PApi::G2PImportOptions importOptions(spec->variant());
        G2PApi::G2PRuntimeOptions runtimeOptions(spec->variant());
        runtimeOptions.binding = {"cmn", "pinyin"};
        auto executive = spec->createInference(importOptions, runtimeOptions);
        BOOST_REQUIRE(executive);

        G2PApi::G2PStartInput input;
        input.lyrics = lyrics;
        auto result = static_cast<G2PApi::G2PExecutive *>(executive->get())->start(input);
        BOOST_REQUIRE(result);
        return (*result)->words;
    };

    // Adjacent, so the phrase table still reads the polyphonic character its phrase way.
    const auto adjacent =
        convertWords({"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(adjacent.size(), 2u);
    BOOST_CHECK_EQUAL(adjacent[0].pronunciation, "yin");
    BOOST_CHECK_EQUAL(adjacent[1].pronunciation, "hang");

    // An empty word is skipped by the contract, and that gap is not adjacency: on its own the
    // character takes its first reading.
    const auto separated = convertWords({"\xE9\x93\xB6", "", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(separated.size(), 3u);
    BOOST_CHECK(separated[1].mode == G2PApi::Mode::Skip);
    BOOST_CHECK_EQUAL(separated[2].pronunciation, "xing");
}

/// A word the engine has no reading for reaches the chain's fallback rather than being handed back
/// with a pronunciation and an error at the same time, which is what the ported engine did.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_FallsBackThroughTheChain) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, "singer-zh", "cmn", {"\xE9\xBE\x99", "zhong"});
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    // Not in this fixture's dictionary: the fallback returns the original word, and a word the
    // fallback produced is a success.
    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "\xE9\xBE\x99");
    BOOST_CHECK(words[0].hitStage == LinguistApi::HitStage::Fallback);

    // Already a pinyin syllable, so the verify step marked it copy and no engine ever saw it.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(words[1].pronunciation, "zhong");
}

/// A second dictionary root cannot quietly displace the first.
///
/// The engine resolves its dictionaries through a process global that its constructor reads, so
/// two roots in one process means whichever loaded last wins and the other converts nothing. The
/// arbiter turns that into a load failure that names both roots.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_RefusesASecondDictionaryRoot) {
    srt::SynthUnit unit;
    configure(unit);

    auto first =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine", srt::SynthUnit::Load);
    if (!first) {
        BOOST_FAIL("the first engine package should have loaded: " + first.error().toString());
    }

    auto second = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine-rival",
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!second, "a second dictionary root should have been refused");
    const auto message = second.error().toString();
    BOOST_CHECK_MESSAGE(message.find("already in use") != std::string::npos,
                        "the diagnostic should say what the clash is: " + message);
    BOOST_CHECK_MESSAGE(message.find("pinyin-engine-rival") != std::string::npos,
                        "the diagnostic should name the root that was refused: " + message);
}

/// A load that fails after the pinyin module has claimed the root must give the root back.
///
/// The claim is process wide and the upper specification requires state a load transaction creates
/// to be transaction private and fully undone by rollback. It was neither: a package that never
/// finished loading kept the root for the life of the plugin, so every other root was refused from
/// then on — a package that does not work disabling one that does.
///
/// Both loads share one SynthUnit on purpose. Destroying a unit unloads its interpreter plugins,
/// and with them the arbiter singleton, so two units would hide exactly the leak under test.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_ReturnsTheRootWhenTheLoadFails) {
    srt::SynthUnit unit;
    configure(unit);

    auto broken =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-rollback", srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!broken, "the rollback fixture is supposed to fail to load");
    // It has to fail for a reason other than the claim itself, or the test proves nothing: the
    // pinyin module must have got far enough to take the claim before the other module failed.
    const auto message = broken.error().toString();
    BOOST_REQUIRE_MESSAGE(message.find("thisKeyDoesNotExist") != std::string::npos,
                          "the fixture should fail on its broken module, not on the root: " +
                              message);

    // Nothing that claimed a root is loaded now, so a different root must be available.
    auto other =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine", srt::SynthUnit::Load);
    if (!other) {
        BOOST_FAIL("the failed load left the dictionary root reserved: " +
                   other.error().toString());
    }
}

/// And an unload must give it back too, for the same reason: the claim lasts exactly as long as
/// the module instance that took it, no longer.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_ReturnsTheRootWhenUnloaded) {
    srt::SynthUnit unit;
    configure(unit);

    {
        auto first = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine",
                                      srt::SynthUnit::Load);
        if (!first) {
            BOOST_FAIL("the engine package should have loaded: " + first.error().toString());
        }
    }
    // The handle was the only owner, so the package is unloaded here.

    auto rival = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine-rival",
                                  srt::SynthUnit::Load);
    if (!rival) {
        BOOST_FAIL("unloading the first root should have freed it: " + rival.error().toString());
    }
}

/// The engine takes a character's alternatives from its per character table but may take the
/// reading itself from a phrase, so the two disagree exactly where a phrase decided the reading.
/// The contract has the pronunciation lead its candidates, so the variant puts it there.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_LeadsCandidatesWithThePronunciation) {
    srt::SynthUnit unit;
    configure(unit);

    // On its own the polyphonic character takes its first table reading, so the two agree.
    const auto alone = convert(unit, "singer-zh", "cmn", {"\xE8\xA1\x8C"});
    BOOST_REQUIRE(!alone[0].candidates.empty());
    BOOST_CHECK_EQUAL(alone[0].candidates.front(), alone[0].pronunciation);
    BOOST_CHECK_EQUAL(alone[0].candidates.size(), 2u);

    // In its phrase the reading comes from elsewhere, which is where the two would part company.
    const auto phrase = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(phrase.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[1].pronunciation, "hang");
    BOOST_REQUIRE(!phrase[1].candidates.empty());
    BOOST_CHECK_EQUAL(phrase[1].candidates.front(), "hang");
    // The other reading is still offered, just no longer first.
    BOOST_CHECK_EQUAL(phrase[1].candidates.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[1].candidates[1], "xing");
}

/// The same ordering, on the engine backed chain. A ruled-on word must not join a conversion run
/// either — the engine reads its neighbours, and a word the contract removed is not one.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_HonoursTheInputOrdering) {
    srt::SynthUnit unit;
    configure(unit);

    // 银, empty, 行 — if the empty word did not break the run, the phrase would still match and
    // the last word would read hang instead of xing.
    const auto words = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(words.size(), 3u);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "yin");
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Skip);
    BOOST_CHECK(words[1].pronunciation.empty());
    BOOST_CHECK_EQUAL(words[2].pronunciation, "xing");
}

BOOST_AUTO_TEST_SUITE_END()
