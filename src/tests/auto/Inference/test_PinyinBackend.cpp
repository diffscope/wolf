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

    /// Converts one batch of lyrics through a language declared by a singer.
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

/// Checks the purpose of the shared backend: Mandarin and Cantonese resolve through one dictionary
/// root, so both work in one process. Previously each engine shipped its own dictionary tree, and
/// the second tree loaded silently disabled the first.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_ServesBothLanguagesAtOnce) {
    srt::SynthUnit unit;
    configure(unit);

    const auto mandarin = convert(unit, "singer-zh", "cmn", {"\xE4\xB8\xAD", "\xE5\x9B\xBD"});
    BOOST_REQUIRE_EQUAL(mandarin.size(), 2u);
    BOOST_CHECK_EQUAL(mandarin[0].pronunciation, "zhong");
    BOOST_CHECK_EQUAL(mandarin[1].pronunciation, "guo");

    // The other engine, in the same unit and the same process.
    const auto cantonese = convert(unit, "singer-zh", "yue", {"\xE4\xB8\xAD", "\xE5\x9B\xBD"});
    BOOST_REQUIRE_EQUAL(cantonese.size(), 2u);
    BOOST_CHECK_EQUAL(cantonese[0].pronunciation, "zung");
    BOOST_CHECK_EQUAL(cantonese[1].pronunciation, "gwok");
}

/// The two languages of one voicebank must warm concurrently, not sequentially.
///
/// Building an engine reads the cpp-pinyin dictionaries. The arbiter previously held its
/// process-wide lock across the whole construction, so the engines in a process were built
/// sequentially. The lock now covers only the publication of the global dictionary path, and
/// construction runs outside the lock. This case guards that behavior and, under ThreadSanitizer,
/// also checks the upstream constructor for data races.
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
            // Deletion detaches a child from its pipeline; children that are not deleted
            // accumulate.
            delete *linguist;
        }
    };

    std::thread mandarin(run, "cmn", "zhong");
    std::thread cantonese(run, "yue", "zung");
    mandarin.join();
    cantonese.join();

    BOOST_CHECK_EQUAL(wrong.load(), 0);
}

/// The neighbours of a word determine its reading, so the chain passes the backend runs of
/// adjacent words rather than one flat list of all remaining words.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_KeepsPhraseContext) {
    srt::SynthUnit unit;
    configure(unit);

    // In isolation, the polyphonic character takes its first reading.
    const auto alone = convert(unit, "singer-zh", "cmn", {"\xE8\xA1\x8C"});
    BOOST_CHECK_EQUAL(alone[0].pronunciation, "xing");

    // Within its phrase, the character takes the other reading.
    const auto phrase = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(phrase.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[0].pronunciation, "yin");
    BOOST_CHECK_EQUAL(phrase[1].pronunciation, "hang");

    // A word that the chain classifies as Copy lies between the characters and breaks the run, so
    // the phrase no longer matches. Without run splitting, the engine would treat the two
    // characters as adjacent.
    const auto broken =
        convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "zhong", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(broken.size(), 3u);
    BOOST_CHECK(broken[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(broken[2].pronunciation, "xing");
}

/// The same rule applies if a host binds the backend directly instead of through the chain.
///
/// The chain splits its batches into runs of convertible words before any call, so the chain never
/// exposed this defect. The backend must still split runs itself, because a host that binds the
/// G2P contract directly passes every word of the song, including gaps. Flattening the batch made
/// the characters on either side of a skipped word adjacent, so they were read as a phrase that
/// the song does not contain.
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

    // One executive per call, which is not reused afterwards: on an Expected<unique_ptr<...>>,
    // the arrow operator accesses the stored pointer, so calling reset() through it destroys the
    // executive instead of preparing it for another batch.
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

    // The characters are adjacent, so the phrase table still assigns the phrase reading to the
    // polyphonic character.
    const auto adjacent =
        convertWords({"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(adjacent.size(), 2u);
    BOOST_CHECK_EQUAL(adjacent[0].pronunciation, "yin");
    BOOST_CHECK_EQUAL(adjacent[1].pronunciation, "hang");

    // The contract skips an empty word, and the gap breaks adjacency: in isolation, the character
    // takes its first reading.
    const auto separated = convertWords({"\xE9\x93\xB6", "", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(separated.size(), 3u);
    BOOST_CHECK(separated[1].mode == G2PApi::Mode::Skip);
    BOOST_CHECK_EQUAL(separated[2].pronunciation, "xing");
}

/// A word for which the engine has no reading reaches the fallback of the chain. The ported engine
/// instead returned such a word with both a pronunciation and an error.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_FallsBackThroughTheChain) {
    srt::SynthUnit unit;
    configure(unit);

    const auto words = convert(unit, "singer-zh", "cmn", {"\xE9\xBE\x99", "zhong"});
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    // The word is absent from the dictionary of this fixture: the fallback returns the original
    // word, and a word produced by the fallback counts as a success.
    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "\xE9\xBE\x99");
    BOOST_CHECK(words[0].hitStage == LinguistApi::HitStage::Fallback);

    // The word is already a pinyin syllable, so the verify step marked it as Copy and no engine
    // received it.
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(words[1].pronunciation, "zhong");
}

/// A second dictionary root cannot silently displace the first.
///
/// The engine resolves its dictionaries through a process-global variable read by its
/// constructor. With two roots in one process, the root loaded last takes effect and the other
/// root converts nothing. The arbiter converts that condition into a load failure that names both
/// roots.
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
    BOOST_REQUIRE_MESSAGE(!second, "a second dictionary root should have been rejected");
    const auto message = second.error().toString();
    BOOST_CHECK_MESSAGE(message.find("already in use") != std::string::npos,
                        "the diagnostic should say what the clash is: " + message);
    BOOST_CHECK_MESSAGE(message.find("pinyin-engine-rival") != std::string::npos,
                        "the diagnostic should name the rejected root: " + message);
}

/// A load that fails after the pinyin module has claimed the root must release the root.
///
/// The claim is process-wide, and the upstream specification requires state created by a load
/// transaction to be private to the transaction and fully reverted by rollback. The claim
/// previously satisfied neither requirement: a package that failed to load kept the root for the
/// lifetime of the plugin, so every other root was rejected afterwards, and a defective package
/// disabled a working package.
///
/// Both loads deliberately share one SynthUnit. Destroying a unit unloads its interpreter plugins
/// and therefore the arbiter singleton, so two units would hide the leak under test.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_ReturnsTheRootWhenTheLoadFails) {
    srt::SynthUnit unit;
    configure(unit);

    auto broken =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-rollback", srt::SynthUnit::Load);
    BOOST_REQUIRE_MESSAGE(!broken, "the rollback fixture must fail to load");
    // The load must fail for a reason other than the claim itself; otherwise the test proves
    // nothing. The pinyin module must have taken the claim before the other module failed.
    const auto message = broken.error().toString();
    BOOST_REQUIRE_MESSAGE(message.find("thisKeyDoesNotExist") != std::string::npos,
                          "the fixture should fail on its broken module, not on the root: " +
                              message);

    // No module that claimed a root remains loaded, so a different root must be available.
    auto other =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine", srt::SynthUnit::Load);
    if (!other) {
        BOOST_FAIL("the failed load left the dictionary root reserved: " +
                   other.error().toString());
    }
}

/// An unload must also release the root, for the same reason: the claim lasts exactly as long as
/// the module instance that took it.
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
    // The handle was the only owner, so the package is unloaded at this point.

    auto rival = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "pinyin-engine-rival",
                                  srt::SynthUnit::Load);
    if (!rival) {
        BOOST_FAIL("unloading the first root should have freed it: " + rival.error().toString());
    }
}

/// The engine takes the alternatives of a character from its per-character table but may take the
/// reading itself from a phrase, so the two differ exactly where a phrase determined the reading.
/// The contract requires the pronunciation to be the first candidate, so the variant places it
/// first.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_LeadsCandidatesWithThePronunciation) {
    srt::SynthUnit unit;
    configure(unit);

    // In isolation, the polyphonic character takes its first table reading, so the two agree.
    const auto alone = convert(unit, "singer-zh", "cmn", {"\xE8\xA1\x8C"});
    BOOST_REQUIRE(!alone[0].candidates.empty());
    BOOST_CHECK_EQUAL(alone[0].candidates.front(), alone[0].pronunciation);
    BOOST_CHECK_EQUAL(alone[0].candidates.size(), 2u);

    // Within its phrase, the reading comes from the phrase table, so the two would differ.
    const auto phrase = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(phrase.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[1].pronunciation, "hang");
    BOOST_REQUIRE(!phrase[1].candidates.empty());
    BOOST_CHECK_EQUAL(phrase[1].candidates.front(), "hang");
    // The other reading is still offered, but no longer first.
    BOOST_CHECK_EQUAL(phrase[1].candidates.size(), 2u);
    BOOST_CHECK_EQUAL(phrase[1].candidates[1], "xing");
}

/// Checks the same ordering on the engine-backed chain. A word already decided by a rule must not
/// join a conversion run either, because the engine reads the neighbours of each word and a word
/// removed by the contract is not a neighbour.
BOOST_AUTO_TEST_CASE(test_PinyinBackend_HonoursTheInputOrdering) {
    srt::SynthUnit unit;
    configure(unit);

    // 银, empty, 行: if the empty word did not break the run, the phrase would still match and
    // the last word would read hang instead of xing.
    const auto words = convert(unit, "singer-zh", "cmn", {"\xE9\x93\xB6", "", "\xE8\xA1\x8C"});
    BOOST_REQUIRE_EQUAL(words.size(), 3u);
    BOOST_CHECK_EQUAL(words[0].pronunciation, "yin");
    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Skip);
    BOOST_CHECK(words[1].pronunciation.empty());
    BOOST_CHECK_EQUAL(words[2].pronunciation, "xing");
}

BOOST_AUTO_TEST_SUITE_END()
