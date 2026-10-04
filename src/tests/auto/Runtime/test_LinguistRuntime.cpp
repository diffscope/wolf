#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
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

    /// Configures \a unit so that it can load the singer package and all packages that it
    /// depends on.
    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit,
                              {wolf::test::fixtureRoot()});
    }

    LinguistApi::WolfPipelineExtension *extensionOf(srt::ContribSpec *singer) {
        auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
            *singer->as<srt::SingerSpec>());
        return base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
    }

}

BOOST_AUTO_TEST_SUITE(test_LinguistRuntime)

/// Traverses the whole tree from the singer through the pipeline and the linguist to the three
/// inference executives, with every step made through createChild on an import that the
/// declaration contains. All of L4 depends on this structure, so the structure is checked before
/// anything is built on top of it.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_WalksTheExecutiveTree) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the singer package should have loaded: " + handle.error().toString());
    }

    auto *singer = handle->contribution("singer", "s");
    BOOST_REQUIRE(singer != nullptr);

    auto *extension = extensionOf(singer);
    BOOST_REQUIRE_MESSAGE(extension != nullptr, "the pipeline extension should have been mounted");
    // The single-step lookup finds the same extension as the two-step lookup.
    BOOST_CHECK(LinguistApi::WolfPipelineExtension::from(*singer->as<srt::SingerSpec>()) ==
                extension);
    BOOST_REQUIRE_EQUAL(extension->languages().size(), 1u);
    BOOST_CHECK_EQUAL(extension->languages()[0], "zxx");
    BOOST_CHECK_EQUAL(extension->defaultLanguage(), "zxx");

    const auto *located = extension->locate("zxx");
    BOOST_REQUIRE(located != nullptr);
    BOOST_CHECK_EQUAL(located->contributionId(), "zxx-passthrough");
    BOOST_CHECK(extension->locate("cmn") == nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    if (!pipeline) {
        BOOST_FAIL("the pipeline should have been created: " + pipeline.error().toString());
    }
    auto *wolfPipeline = (*pipeline)->as<LinguistApi::WolfPipelineExecutive>();
    BOOST_REQUIRE(wolfPipeline != nullptr);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist = wolfPipeline->createLinguist("zxx", options);
    if (!linguist) {
        BOOST_FAIL("the linguist executive should have been created: " +
                   linguist.error().toString());
    }

    // Executive-level diagnostics are fixed for the lifetime of the executive, so they are stored
    // on the executive rather than on every word.
    BOOST_CHECK_EQUAL((*linguist)->binding().language, "zxx");
    BOOST_CHECK_EQUAL((*linguist)->binding().scheme, "passthrough");
    BOOST_CHECK_EQUAL((*linguist)->g2pContribution().contributionId(), "zxx-g2p");

    // A handle the singer does not declare is rejected rather than silently ignored.
    auto missing = wolfPipeline->createLinguist("cmn", options);
    BOOST_CHECK(!missing);
}

/// The passthrough language converts digits and punctuation to themselves, as the three merged
/// legacy suites did. Running it end to end also exercises all three chain stages.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_ConvertsThroughTheChain) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);

    auto *extension = extensionOf(handle->contribution("singer", "s"));
    BOOST_REQUIRE(extension != nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Onsets;
    input.words.push_back({"123", std::nullopt, std::nullopt});
    input.words.push_back({",", std::nullopt, std::nullopt});

    // A word whose phoneme layer the user pinned bypasses both conversion stages.
    LinguistApi::LinguistWordInput pinned;
    pinned.lyric = "x";
    pinned.locked = LinguistApi::LockedPhonemes{
        {"a",  "b"  },
        {true, false}
    };
    input.words.push_back(pinned);

    auto result = (*linguist)->start(input);
    if (!result) {
        BOOST_FAIL("the conversion should have run: " + result.error().toString());
    }
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 3u);

    const auto &digits = (*result)->words[0];
    BOOST_CHECK(digits.mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK(digits.error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(digits.pronunciation, "123");
    BOOST_REQUIRE_EQUAL(digits.phonemes.size(), 1u);
    BOOST_CHECK_EQUAL(digits.phonemes[0], "123");
    BOOST_CHECK_EQUAL(digits.onsets.size(), digits.phonemes.size());

    BOOST_CHECK_EQUAL((*result)->words[1].pronunciation, ",");

    const auto &locked = (*result)->words[2];
    BOOST_CHECK(locked.hitStage == LinguistApi::HitStage::Locked);
    BOOST_REQUIRE_EQUAL(locked.phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(locked.phonemes[1], "b");
    BOOST_CHECK_EQUAL(locked.onsets.size(), 2u);
    BOOST_CHECK(locked.onsets[0]);

    BOOST_CHECK((*linguist)->state() == srt::ITask::Succeeded);
}

/// A pinned phoneme layer is granted at the depth that asks for it, so one locked word of a batch
/// does not carry fields that the depth it was converted at does not grant.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RequiresThePhonemeDepthForAPinnedLayer) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);

    auto *extension = extensionOf(handle->contribution("singer", "s"));
    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Pronunciation;
    input.words.push_back({"123", std::nullopt, std::nullopt});
    LinguistApi::LinguistWordInput pinned;
    pinned.lyric = "x";
    pinned.locked = LinguistApi::LockedPhonemes{
        {"a",  "b"  },
        {true, false}
    };
    input.words.push_back(pinned);

    auto result = (*linguist)->start(input);
    if (!result) {
        BOOST_FAIL("the conversion should have run: " + result.error().toString());
    }
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 2u);

    // The word the stage converted carries no phonemes at this depth, and the locked word joins it
    // instead of answering with the layer it pinned while the rest of the batch answers by depth.
    BOOST_CHECK((*result)->words[0].phonemes.empty());
    const auto &locked = (*result)->words[1];
    BOOST_CHECK(locked.hitStage == LinguistApi::HitStage::Locked);
    BOOST_CHECK_EQUAL(locked.pronunciation, "x");
    BOOST_CHECK(locked.phonemes.empty());
    BOOST_CHECK(locked.onsets.empty());
}

/// At Phonemes the pinned layer grants the phonemes and still withholds the onsets.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RequiresTheOnsetDepthForAPinnedLayer) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);

    auto *extension = extensionOf(handle->contribution("singer", "s"));
    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Phonemes;
    LinguistApi::LinguistWordInput pinned;
    pinned.lyric = "x";
    pinned.locked = LinguistApi::LockedPhonemes{
        {"a",  "b"  },
        {true, false}
    };
    input.words.push_back(pinned);

    auto result = (*linguist)->start(input);
    if (!result) {
        BOOST_FAIL("the conversion should have run: " + result.error().toString());
    }
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);

    const auto &locked = (*result)->words[0];
    BOOST_REQUIRE_EQUAL(locked.phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(locked.phonemes[1], "b");
    BOOST_CHECK(locked.onsets.empty());
}

/// A conversion that stops at the pronunciation layer does not build the downstream executives,
/// so a caller that requires only pronunciations does not incur the cost of phoneme conversion.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_StopsAtRequestedDepth) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    BOOST_REQUIRE(handle);

    auto *extension = extensionOf(handle->contribution("singer", "s"));
    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Pronunciation;
    input.words.push_back({"42", std::nullopt, std::nullopt});

    auto result = (*linguist)->start(input);
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "42");
    BOOST_CHECK((*result)->words[0].phonemes.empty());
}

/// The asynchronous interface must be the framework implementation, not a hand-written copy of it.
///
/// The case checks three properties that the earlier implementation violated: waitForFinished()
/// returned immediately while the conversion was still running, a second concurrent execution was
/// accepted although the contract allows one execution per executive at a time, and the worker was
/// a detached thread that no caller could wait for, which allowed a Package to be unloaded during
/// a running conversion. The properties now hold because the executive holds an srt::ITask and
/// forwards to it.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_AsyncFaceIsTheFrameworkTask) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-zxx", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the singer package should have loaded: " + handle.error().toString());
    }
    auto *extension = extensionOf(handle->contribution("singer", "s"));
    BOOST_REQUIRE(extension != nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);

    auto input = std::make_shared<LinguistApi::LinguistConvertInput>();
    input->depth = LinguistApi::Depth::Onsets;
    input->words.push_back({"1", std::nullopt, std::nullopt});

    std::atomic<bool> finished{false};
    std::atomic<bool> converted{false};
    // The callback delays its completion so that the running state is observable rather than
    // the outcome of a race.
    auto started = (*linguist)->startAsync(
        input, [&](srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>> result) {
            converted = result.operator bool();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            finished = true;
        });
    BOOST_REQUIRE_MESSAGE(started, "startAsync should have started: " +
                                       (started ? std::string() : started.error().toString()));

    // A second execution while the first is running is rejected; it is neither queued nor run.
    auto second = (*linguist)->startAsync(
        input, [](srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>>) {});
    BOOST_CHECK_MESSAGE(!second, "a second concurrent execution should have been rejected");

    // waitForFinished() blocks until the callback has completed.
    BOOST_CHECK((*linguist)->waitForFinished());
    BOOST_CHECK_MESSAGE(finished.load(), "waitForFinished() returned while the worker was running");
    BOOST_CHECK(converted.load());
    BOOST_CHECK((*linguist)->state() == srt::ITask::Succeeded);

    delete *linguist;
}

/// A stage that returns fewer words than it received fails the conversion instead of truncating
/// it.
///
/// Truncation left every word after the cut with an empty pronunciation and no error, which is
/// indistinguishable from a conversion to an empty result, and those words then continued down the
/// chain. The chain variant already rejects a short batch from its backend, and the session
/// rejects a short batch from the executive; this case covers the same rule at the third location
/// where a short batch can occur. No shipped module miscounts, so the test stub miscounts on
/// request, which verifies that the guard is triggered.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesAShortBatch) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(wolf::test::fixtureRoot() / "singer-miscount", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the miscount fixture should have loaded: " + handle.error().toString());
    }
    auto *extension = extensionOf(handle->contribution("singer", "s"));
    BOOST_REQUIRE(extension != nullptr);

    LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
    auto pipeline = extension->createPipeline(pipelineOptions);
    BOOST_REQUIRE(pipeline);

    LinguistApi::LinguistRuntimeOptions options;
    auto linguist =
        (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist("nld", options);
    BOOST_REQUIRE(linguist);

    LinguistApi::LinguistConvertInput input;
    input.depth = LinguistApi::Depth::Onsets;
    input.words.push_back({"one", std::nullopt, std::nullopt});
    input.words.push_back({"two", std::nullopt, std::nullopt});

    auto result = (*linguist)->start(input);
    BOOST_REQUIRE_MESSAGE(!result, "a short batch should have failed the conversion");
    const auto message = result.error().toString();
    BOOST_CHECK_MESSAGE(message.find("g2p") != std::string::npos,
                        "the diagnostic should name the stage: " + message);
    BOOST_CHECK((*linguist)->state() == srt::ITask::Failed);

    delete *linguist;
}

/// Checks the singer side of the validator on the load path: a language routed to an import that
/// is not a linguist, and a language handle that differs from the language of its target linguist,
/// are both rejected before Commit rather than detected by a failing conversion.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesALanguageRoutedToANonLinguist) {
    srt::SynthUnit unit;
    configure(unit);
    auto handle = unit.openPackage(wolf::test::fixtureRoot() / "singer-wrong-target",
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("not a linguist contribution") != std::string::npos, message);
}

BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesALanguageHandleTheLinguistDoesNotCarry) {
    srt::SynthUnit unit;
    configure(unit);
    auto handle = unit.openPackage(wolf::test::fixtureRoot() / "singer-lang-mismatch",
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("whose language is zxx") != std::string::npos, message);
}

BOOST_AUTO_TEST_SUITE_END()
