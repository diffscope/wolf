#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace S2PApi = wolf::Api::S2P::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace {

    /// Builds a one-module package around a script, so the variant is exercised through the loader
    /// rather than through the sandbox directly.
    fs::path writePackage(const std::string &name, const std::string &contract,
                          const std::string &script) {
        const auto root = fs::temp_directory_path() / ("wolf-lua-" + name);
        fs::remove_all(root);
        fs::create_directories(root / "inferences" / "m");

        std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-lua-)" << name << R"(",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": { "inference": [ { "id": "m", "path": "./inferences/m/inference.json" } ] }
})";
        std::ofstream(root / "inferences" / "m" / "inference.json") << R"({
    "interface": "org.openvpi.wolf.inference.)" << contract << R"(",
    "level": 1,
    "variant": "lua",
    "configuration": { "file": "script.lua" }
})";
        std::ofstream(root / "inferences" / "m" / "script.lua") << script;
        return root;
    }

    srt::Expected<srt::PackageHandle> load(srt::SynthUnit &unit, const fs::path &package) {
        wolf::test::configure(unit, {});
        return unit.openPackage(package, srt::SynthUnit::Load);
    }

    std::vector<std::string> convert(srt::PackageHandle &handle, const std::string &pronunciation) {
        auto *spec = handle.contribution("inference", "m")->as<srt::InferenceSpec>();
        S2PApi::S2PImportOptions options(spec->variant());
        S2PApi::S2PRuntimeOptions runtime(spec->variant());
        auto executive = spec->createInference(options, runtime);
        BOOST_REQUIRE(executive);

        S2PApi::S2PStartInput input;
        input.pronunciations.push_back(pronunciation);
        auto result = static_cast<S2PApi::S2PExecutive *>(executive->get())->start(input);
        BOOST_REQUIRE(result);
        auto phonemes = (*result)->phonemes.front();
        executive->reset();
        return phonemes;
    }

    std::vector<bool> mark(srt::PackageHandle &handle, const std::vector<std::string> &phonemes) {
        auto *spec = handle.contribution("inference", "m")->as<srt::InferenceSpec>();
        OnsetApi::OnsetImportOptions options(spec->variant());
        OnsetApi::OnsetRuntimeOptions runtime(spec->variant());
        auto executive = spec->createInference(options, runtime);
        BOOST_REQUIRE(executive);

        OnsetApi::OnsetStartInput input;
        input.phonemes.push_back(phonemes);
        auto result = static_cast<OnsetApi::OnsetExecutive *>(executive->get())->start(input);
        BOOST_REQUIRE(result);
        auto onsets = (*result)->onsets.front();
        executive->reset();
        return onsets;
    }

}

BOOST_AUTO_TEST_SUITE(test_LuaVariants)

/// The S2P script turns one pronunciation into a list of phonemes.
BOOST_AUTO_TEST_CASE(test_LuaVariants_ScriptedS2P) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("s2p", "S2P", R"(
function s2p(pronunciation)
    local out = {}
    for piece in string.gmatch(pronunciation, "[^%s]+") do
        out[#out + 1] = piece
    end
    return out
end
)"));
    if (!handle) {
        BOOST_FAIL("the scripted S2P should have loaded: " + handle.error().toString());
    }
    const auto phonemes = convert(*handle, " zh ong ");
    BOOST_REQUIRE_EQUAL(phonemes.size(), 2u);
    BOOST_CHECK_EQUAL(phonemes[0], "zh");
    BOOST_CHECK_EQUAL(phonemes[1], "ong");
}

/// The Onset script marks positions, and returns exactly as many flags as it was given phonemes.
BOOST_AUTO_TEST_CASE(test_LuaVariants_ScriptedOnset) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("onset", "Onset", R"(
local vowels = { a = true, o = true, i = true }
function markonset(phonemes)
    local out = {}
    for i = 1, #phonemes do
        out[i] = not vowels[phonemes[i]]
    end
    return out
end
)"));
    if (!handle) {
        BOOST_FAIL("the scripted Onset should have loaded: " + handle.error().toString());
    }
    const auto onsets = mark(*handle, {"zh", "o", "n"});
    BOOST_REQUIRE_EQUAL(onsets.size(), 3u);
    BOOST_CHECK(onsets[0]);
    BOOST_CHECK(!onsets[1]);
    BOOST_CHECK(onsets[2]);
}

/// LuaJIT follows Lua 5.1, which has no utf8 library, so the sandbox supplies one. Without it a
/// script for anything but ASCII could not walk its own input.
BOOST_AUTO_TEST_CASE(test_LuaVariants_SuppliesUtf8) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("utf8", "S2P", R"(
function s2p(pronunciation)
    local out = {}
    for _, code in utf8.codes(pronunciation) do
        out[#out + 1] = utf8.char(code)
    end
    out[#out + 1] = tostring(utf8.len(pronunciation))
    return out
end
)"));
    if (!handle) {
        BOOST_FAIL("the script should have loaded: " + handle.error().toString());
    }
    // Three characters, two of them outside ASCII.
    const auto phonemes = convert(*handle, "a\xE4\xB8\xAD\xC3\xA9");
    BOOST_REQUIRE_EQUAL(phonemes.size(), 4u);
    BOOST_CHECK_EQUAL(phonemes[0], "a");
    BOOST_CHECK_EQUAL(phonemes[1], "\xE4\xB8\xAD");
    BOOST_CHECK_EQUAL(phonemes[2], "\xC3\xA9");
    BOOST_CHECK_EQUAL(phonemes[3], "3");
}

/// A language package is data. The sandbox takes away every way of reaching outside the process,
/// so a script that tries finds nothing there.
BOOST_AUTO_TEST_CASE(test_LuaVariants_RemovesTheWayOut) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("sandbox", "S2P", R"(
function s2p(pronunciation)
    local reachable = {}
    for _, name in ipairs({"io", "os", "debug", "package", "require", "dofile", "loadfile",
                           "load", "loadstring"}) do
        if _G[name] ~= nil then
            reachable[#reachable + 1] = name
        end
    end
    return reachable
end
)"));
    if (!handle) {
        BOOST_FAIL("the script should have loaded: " + handle.error().toString());
    }
    const auto reachable = convert(*handle, "");
    std::string leaked;
    for (const auto &name : reachable) {
        leaked += (leaked.empty() ? "" : ", ") + name;
    }
    BOOST_CHECK_MESSAGE(reachable.empty(), "still reachable: " + leaked);
}

/// A script that does not compile, or that never defines what the contract asks for, fails the
/// package rather than the first conversion that reaches it.
BOOST_AUTO_TEST_CASE(test_LuaVariants_RejectsBrokenScripts) {
    {
        srt::SynthUnit unit;
        auto handle = load(unit, writePackage("broken", "S2P", "function s2p( end\n"));
        BOOST_REQUIRE(!handle);
        BOOST_CHECK(handle.error().toString().find("does not compile") != std::string::npos);
    }
    {
        srt::SynthUnit unit;
        auto handle = load(unit, writePackage("empty", "S2P", "local x = 1\n"));
        BOOST_REQUIRE(!handle);
        BOOST_CHECK(handle.error().toString().find("no global function") != std::string::npos);
    }
    {
        srt::SynthUnit unit;
        auto handle =
            load(unit, writePackage("wrongname", "Onset", "function s2p(p) return {} end\n"));
        BOOST_REQUIRE(!handle);
        BOOST_CHECK(handle.error().toString().find("markonset") != std::string::npos);
    }
}

/// A script returning the wrong shape is a failure, not something to paper over: an onset table
/// shorter than its input would silently leave the tail unmarked.
BOOST_AUTO_TEST_CASE(test_LuaVariants_RejectsWrongShapedResults) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("shape", "Onset", R"(
function markonset(phonemes)
    return { true }
end
)"));
    BOOST_REQUIRE(handle);

    auto *spec = handle->contribution("inference", "m")->as<srt::InferenceSpec>();
    OnsetApi::OnsetImportOptions options(spec->variant());
    OnsetApi::OnsetRuntimeOptions runtime(spec->variant());
    auto executive = spec->createInference(options, runtime);
    BOOST_REQUIRE(executive);

    OnsetApi::OnsetStartInput input;
    input.phonemes.push_back({"a", "b", "c"});
    auto result = static_cast<OnsetApi::OnsetExecutive *>(executive->get())->start(input);
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().toString().find("1 flags for 3 phonemes") != std::string::npos);
    executive->reset();
}

/// A script can loop on its own, so a word boundary is not a boundary at all. The interpreter is
/// told to give up between instruction batches, which is what keeps a bad package from holding a
/// host forever. Without this the test would not fail, it would hang.
BOOST_AUTO_TEST_CASE(test_LuaVariants_StopsARunawayScript) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("runaway", "S2P", R"(
function s2p(pronunciation)
    while true do end
end
)"));
    BOOST_REQUIRE(handle);

    auto *spec = handle->contribution("inference", "m")->as<srt::InferenceSpec>();
    S2PApi::S2PImportOptions options(spec->variant());
    S2PApi::S2PRuntimeOptions runtime(spec->variant());
    auto executive = spec->createInference(options, runtime);
    BOOST_REQUIRE(executive);
    auto *s2p = static_cast<S2PApi::S2PExecutive *>(executive->get());

    // Stops once the script is really running; see test_PipeChain for why not after a delay.
    // Stops once the script is really running, then a moment later; see test_PipeChain for why
    // the wait is on the state and not on a delay alone.
    std::thread stopper([s2p] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s2p->state() != srt::ITask::Running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        s2p->stop();
    });

    S2PApi::S2PStartInput input;
    input.pronunciations.push_back("anything");
    auto result = s2p->start(input);
    stopper.join();

    // A stopped conversion is not a failed one: the caller gets what was finished, and the state
    // says why there is no more.
    BOOST_REQUIRE(result);
    BOOST_CHECK(s2p->state() == srt::ITask::Canceled);
    BOOST_CHECK((*result)->phonemes.empty());
    executive->reset();
}

/// A stopped conversion keeps what it finished. The first word converts, the second never
/// returns, and the batch comes back one entry long rather than empty or wrong.
BOOST_AUTO_TEST_CASE(test_LuaVariants_KeepsWhatItFinished) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("partial", "S2P", R"(
function s2p(pronunciation)
    if pronunciation == "loop" then
        while true do end
    end
    return { pronunciation }
end
)"));
    BOOST_REQUIRE(handle);

    auto *spec = handle->contribution("inference", "m")->as<srt::InferenceSpec>();
    S2PApi::S2PImportOptions options(spec->variant());
    S2PApi::S2PRuntimeOptions runtime(spec->variant());
    auto executive = spec->createInference(options, runtime);
    BOOST_REQUIRE(executive);
    auto *s2p = static_cast<S2PApi::S2PExecutive *>(executive->get());

    // Stops once the script is really running; see test_PipeChain for why not after a delay.
    // Stops once the script is really running, then a moment later; see test_PipeChain for why
    // the wait is on the state and not on a delay alone.
    std::thread stopper([s2p] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s2p->state() != srt::ITask::Running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        s2p->stop();
    });

    S2PApi::S2PStartInput input;
    input.pronunciations.push_back("done");
    input.pronunciations.push_back("loop");
    auto result = s2p->start(input);
    stopper.join();

    BOOST_REQUIRE(result);
    BOOST_CHECK(s2p->state() == srt::ITask::Canceled);
    BOOST_REQUIRE_EQUAL((*result)->phonemes.size(), 1u);
    BOOST_CHECK_EQUAL((*result)->phonemes[0].front(), "done");
    executive->reset();
}

/// A stop is aimed at the execution running now, or at the next one if none is: the same rule the
/// linguist executive above these follows. A request that arrived with nothing running cancels
/// the next batch and is consumed by it, so the batch after that runs normally. The linguist
/// executive recognizes a stage cancelled by a request it never made and asks again, which is
/// what keeps a stop left over from an earlier conversion from costing a later one.
BOOST_AUTO_TEST_CASE(test_LuaVariants_AStopWithNothingRunningCancelsTheNextRunOnce) {
    srt::SynthUnit unit;
    auto handle = load(unit, writePackage("carry", "S2P", R"(
function s2p(pronunciation)
    return { pronunciation }
end
)"));
    BOOST_REQUIRE(handle);

    auto spec = handle->contribution("inference", "m")->as<srt::InferenceSpec>();
    S2PApi::S2PImportOptions options(spec->variant());
    S2PApi::S2PRuntimeOptions runtime(spec->variant());
    auto executive = spec->createInference(options, runtime);
    BOOST_REQUIRE(executive);
    auto s2p = static_cast<S2PApi::S2PExecutive *>(executive->get());

    s2p->stop();

    S2PApi::S2PStartInput input;
    input.pronunciations.push_back("a");
    auto cancelled = s2p->start(input);
    BOOST_REQUIRE(cancelled);
    BOOST_CHECK(s2p->state() == srt::ITask::Canceled);
    BOOST_CHECK((*cancelled)->phonemes.empty());

    auto result = s2p->start(input);
    BOOST_REQUIRE(result);
    BOOST_CHECK(s2p->state() == srt::ITask::Succeeded);
    BOOST_REQUIRE_EQUAL((*result)->phonemes.size(), 1u);
    executive->reset();
}

BOOST_AUTO_TEST_SUITE_END()
