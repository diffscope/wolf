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

    /// Builds a unit that can load the singer package and everything under it.
    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit,
                              {fs::path(WOLF_TEST_PACKAGE_DIR), fs::path(WOLF_TEST_FIXTURE_DIR)});
    }

    LinguistApi::WolfPipelineExtension *extensionOf(srt::ContribSpec *singer) {
        auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
            *singer->as<srt::SingerSpec>());
        return base ? base->as<LinguistApi::WolfPipelineExtension>() : nullptr;
    }

}

BOOST_AUTO_TEST_SUITE(test_LinguistRuntime)

/// Walks the whole tree: singer to pipeline to linguist to the three inference executives, every
/// hop through createChild on an import the declaration really carries. This is the shape all of
/// L4 rests on, so it is checked before anything is built on top of it.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_WalksTheExecutiveTree) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-zxx", srt::SynthUnit::Load);
    if (!handle) {
        BOOST_FAIL("the singer package should have loaded: " + handle.error().toString());
    }

    auto *singer = handle->contribution("singer", "s");
    BOOST_REQUIRE(singer != nullptr);

    auto *extension = extensionOf(singer);
    BOOST_REQUIRE_MESSAGE(extension != nullptr, "the pipeline extension should have been mounted");
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

    // Executive level diagnostics are fixed for its lifetime, which is why they live here rather
    // than on every word.
    BOOST_CHECK_EQUAL((*linguist)->binding().language, "zxx");
    BOOST_CHECK_EQUAL((*linguist)->binding().scheme, "passthrough");
    BOOST_CHECK_EQUAL((*linguist)->g2pContribution().contributionId(), "zxx-g2p");

    // A handle the singer does not declare is rejected rather than silently ignored.
    auto missing = wolfPipeline->createLinguist("cmn", options);
    BOOST_CHECK(!missing);
}

/// The passthrough language converts digits and punctuation to themselves, which is what the three
/// merged legacy suites did. Running it end to end also exercises all three chain stages.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_ConvertsThroughTheChain) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-zxx", srt::SynthUnit::Load);
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

    // A word whose phoneme layer the user pinned skips both conversion stages.
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

/// Stopping at the pronunciation layer never builds the downstream executives, which is how a
/// caller that only wants pronunciations avoids paying for phonemes.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_StopsAtRequestedDepth) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-zxx", srt::SynthUnit::Load);
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

/// The asynchronous face has to be the framework's, not a hand rolled copy of it.
///
/// Three things are checked, and all three used to be false: waitForFinished() returned at once
/// while the conversion was still running, a second concurrent execution was accepted although the
/// contract says an executive carries one at a time, and the worker was a detached thread nothing
/// could wait for — which is what let a Package be unloaded out from under a running conversion.
/// They are true now because the executive holds an srt::ITask and forwards to it.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_AsyncFaceIsTheFrameworkTask) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-zxx", srt::SynthUnit::Load);
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
    // The callback lingers so that "still running" is a real state rather than a race the test
    // happens to lose.
    auto started = (*linguist)->startAsync(
        input, [&](srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>> result) {
            converted = result.operator bool();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            finished = true;
        });
    BOOST_REQUIRE_MESSAGE(started, "startAsync should have started: " +
                                       (started ? std::string() : started.error().toString()));

    // A second execution while the first is in flight is refused, not queued and not run.
    auto second = (*linguist)->startAsync(
        input, [](srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>>) {});
    BOOST_CHECK_MESSAGE(!second, "a second concurrent execution should have been refused");

    // And waiting actually waits: the callback has completed by the time this returns.
    BOOST_CHECK((*linguist)->waitForFinished());
    BOOST_CHECK_MESSAGE(finished.load(), "waitForFinished() returned while the worker was running");
    BOOST_CHECK(converted.load());
    BOOST_CHECK((*linguist)->state() == srt::ITask::Succeeded);

    delete *linguist;
}

/// A stage that returns fewer words than it was given fails the conversion instead of truncating
/// it.
///
/// Truncating left every word past the cut carrying an empty pronunciation and no error, which
/// reads as "converted to nothing", and those words then went on down the chain. The chain variant
/// already refused a short batch from its backend and the session refuses one from the executive;
/// this is the same rule at the third place it can happen. Nothing shipped can miscount, so the
/// test stub does it on request — a guard is only worth having once something has proved it fires.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesAShortBatch) {
    srt::SynthUnit unit;
    configure(unit);

    auto handle =
        unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-miscount", srt::SynthUnit::Load);
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

/// The singer side of the validator, on the load path: a language that routes to an import which
/// is not a linguist, and a language handle that disagrees with the linguist it routes to, are
/// both refused before Commit rather than discovered by a failing conversion.
BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesALanguageRoutedToANonLinguist) {
    srt::SynthUnit unit;
    configure(unit);
    auto handle = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-wrong-target",
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("not a linguist contribution") != std::string::npos, message);
}

BOOST_AUTO_TEST_CASE(test_LinguistRuntime_RefusesALanguageHandleTheLinguistDoesNotCarry) {
    srt::SynthUnit unit;
    configure(unit);
    auto handle = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-lang-mismatch",
                                   srt::SynthUnit::Load);
    BOOST_REQUIRE(!handle);
    const auto message = handle.error().toString();
    BOOST_CHECK_MESSAGE(message.find("whose language is zxx") != std::string::npos, message);
}

BOOST_AUTO_TEST_SUITE_END()
