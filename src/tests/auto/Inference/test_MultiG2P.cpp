#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>

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
namespace OnnxApi = ds::Api::Onnx;

namespace {

    /// Where the converted packages are. The shared backend is 14 MiB of models, so it is never
    /// committed; without it this whole file has nothing to run against.
    using wolf::test::convertedRoot;

    /// Everything here needs the shared model backend package, the ONNX driver plugin and an ONNX
    /// Runtime to hand it. Missing any of them is reported to ctest as a skip.
    struct DataOrSkip {
        DataOrSkip() {
            if (!fs::is_directory(convertedRoot() / "wolf-g2p-multi")) {
                wolf::test::skip("no converted backend package; set WOLF_LANG_PACKAGES_SOURCE "
                                 "or run scripts/convert-g2p-packages.py");
            }
            if (!fs::is_directory(fs::path(WOLF_TEST_DRIVER_PLUGIN_DIR)) ||
                std::string_view(WOLF_TEST_ONNXRUNTIME_DIR).empty()) {
                wolf::test::skip("no ONNX driver plugin or ONNX Runtime in this tree");
            }
        }
    };

    void configure(srt::SynthUnit &unit) {
        wolf::test::configure(unit, {fs::path(WOLF_TEST_FIXTURE_DIR), convertedRoot()});
    }

    /// Does what a host does: finds the ONNX driver, initializes it, and registers it as the
    /// backend the whole unit shares. A module never loads a driver of its own.
    bool registerDriver(ds::InferenceDriverFactory &factory, srt::SynthUnit &unit,
                        const fs::path &runtimeDir = fs::path(WOLF_TEST_ONNXRUNTIME_DIR)) {
        std::vector<fs::path> paths = {fs::path(WOLF_TEST_DRIVER_PLUGIN_DIR)};
        factory.setPluginPaths(paths);
        auto *loader = factory.find(OnnxApi::API_NAME);
        if (loader == nullptr) {
            BOOST_TEST_MESSAGE("no ONNX driver plugin present");
            return false;
        }
        auto created = factory.create(loader);
        if (!created) {
            BOOST_TEST_MESSAGE("the driver could not be created: " + created.error().toString());
            return false;
        }
        auto driver = created.take();

        OnnxApi::DriverInitArgs args;
        args.ep = OnnxApi::ExecutionProvider::CPU;
        args.runtimePath = runtimeDir;
        if (auto ready = driver->initialize(args); !ready) {
            BOOST_TEST_MESSAGE("the driver could not be initialized: " + ready.error().toString());
            return false;
        }
        auto added = unit.addRuntimeService(std::move(driver));
        BOOST_REQUIRE_MESSAGE(added, "the driver should have been registered");
        return true;
    }

    /// Deploys ONNX Runtime the way an editor does: into a directory of the application's own
    /// choosing, unrelated to where the package put it and to where the driver plugin lives.
    ///
    /// Symlinks are copied as symlinks, because that is what the payload is — libonnxruntime.so
    /// points at .so.1 points at the real file, and flattening them would deploy three copies.
    fs::path deployRuntimeLikeAHost() {
        const auto staged = fs::temp_directory_path() / "wolf-host-deployed-ort";
        std::error_code ec;
        fs::remove_all(staged, ec);
        fs::create_directories(staged);
        for (const auto &entry : fs::directory_iterator(fs::path(WOLF_TEST_ONNXRUNTIME_DIR))) {
            fs::copy(entry.path(), staged / entry.path().filename(),
                     fs::copy_options::copy_symlinks | fs::copy_options::overwrite_existing);
        }
        return staged;
    }

    std::vector<LinguistApi::LinguistWordOutput>
        convert(srt::SynthUnit &unit, const std::string &language,
                const std::vector<std::string> &lyrics,
                LinguistApi::Depth depth = LinguistApi::Depth::Phonemes) {
        auto handle = unit.openPackage(fs::path(WOLF_TEST_FIXTURE_DIR) / "singer-model",
                                       srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the singer should have loaded: " + handle.error().toString());
        }
        auto *base = srt::ContribSpecExtension::findFromSpec<LinguistApi::WolfPipelineExecutive>(
            *handle->contribution("singer", "s")->as<srt::SingerSpec>());
        BOOST_REQUIRE(base != nullptr);

        LinguistApi::WolfPipelineRuntimeOptions pipelineOptions;
        auto pipeline =
            base->as<LinguistApi::WolfPipelineExtension>()->createPipeline(pipelineOptions);
        BOOST_REQUIRE(pipeline);

        LinguistApi::LinguistRuntimeOptions options;
        auto linguist = (*pipeline)->as<LinguistApi::WolfPipelineExecutive>()->createLinguist(
            language, options);
        if (!linguist) {
            BOOST_FAIL("the linguist should have been created: " + linguist.error().toString());
        }

        LinguistApi::LinguistConvertInput input;
        input.depth = depth;
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

BOOST_TEST_GLOBAL_FIXTURE(DataOrSkip);

BOOST_AUTO_TEST_SUITE(test_MultiG2P)

/// Runs the shipped model end to end: a singer picks a language, the language's chain reaches the
/// shared backend through an import, and the backend runs three ONNX models over a real word.
BOOST_AUTO_TEST_CASE(test_MultiG2P_ConvertsThroughTheSharedModel) {
    // Declared before the unit: the factory holds the plugin code, and every driver made from it
    // must be gone first.
    ds::InferenceDriverFactory factory;
    srt::SynthUnit unit;
    configure(unit);
    BOOST_REQUIRE(registerDriver(factory, unit));

    const auto words = convert(unit, "eng", {"hello", "world"});
    BOOST_REQUIRE_EQUAL(words.size(), 2u);

    for (const auto &word : words) {
        BOOST_CHECK(word.error == wolf::Api::G2P::L1::Error::None);
        BOOST_CHECK(word.mode == wolf::Api::G2P::L1::Mode::Convert);
        BOOST_CHECK_MESSAGE(word.hitStage == LinguistApi::HitStage::Model,
                            "the model should have produced " + word.pronunciation);
        // Space delimited symbols, which is what the whole family agrees a pronunciation is.
        BOOST_CHECK_MESSAGE(word.pronunciation.find(' ') != std::string::npos,
                            "expected several symbols, got: " + word.pronunciation);
        // The phoneme layer splits on those spaces, so the two have to agree.
        BOOST_CHECK(!word.phonemes.empty());
    }

    BOOST_TEST_MESSAGE("hello -> " + words[0].pronunciation);
    BOOST_TEST_MESSAGE("world -> " + words[1].pronunciation);

    // A word this chain classified copy never reaches the model.
    const auto punctuation = convert(unit, "eng", {","});
    BOOST_CHECK(punctuation[0].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(punctuation[0].pronunciation, ",");
}

/// One bundle, several languages, one process. The language id comes from the bundle's own
/// declaration order, so a wrong mapping would show up as the wrong phoneme set rather than as an
/// error.
BOOST_AUTO_TEST_CASE(test_MultiG2P_ServesSeveralLanguages) {
    ds::InferenceDriverFactory factory;
    srt::SynthUnit unit;
    configure(unit);
    BOOST_REQUIRE(registerDriver(factory, unit));

    const auto english = convert(unit, "eng", {"water"});
    const auto german = convert(unit, "deu", {"wasser"});
    BOOST_REQUIRE_EQUAL(english.size(), 1u);
    BOOST_REQUIRE_EQUAL(german.size(), 1u);
    BOOST_CHECK(!english[0].pronunciation.empty());
    BOOST_CHECK(!german[0].pronunciation.empty());
    BOOST_TEST_MESSAGE("eng water -> " + english[0].pronunciation);
    BOOST_TEST_MESSAGE("deu wasser -> " + german[0].pronunciation);

    // The two languages are different phoneme sets, so the same spelling would not converge on the
    // same reading. This is the assertion that a swapped language id would break.
    const auto englishWasser = convert(unit, "eng", {"wasser"});
    BOOST_CHECK_MESSAGE(englishWasser[0].pronunciation != german[0].pronunciation,
                        "both languages produced " + german[0].pronunciation +
                            ", which means the language id did not reach the model");
}

/// Without a driver the package still loads and the chain still runs. The backend says so per
/// word, which is what lets the chain's fallback stand in.
BOOST_AUTO_TEST_CASE(test_MultiG2P_DegradesWithoutADriver) {
    srt::SynthUnit unit;
    configure(unit);
    // No driver registered this time.

    const auto covered = convert(unit, "eng", {"hello"});
    BOOST_REQUIRE_EQUAL(covered.size(), 1u);
    // The chain has a fallback, so the word comes back as itself and counts as a success.
    BOOST_CHECK(covered[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(covered[0].pronunciation, "hello");
    BOOST_CHECK(covered[0].hitStage == LinguistApi::HitStage::Fallback);

    // The other language's chain has no fallback, so the backend's own verdict survives.
    const auto bare = convert(unit, "deu", {"wasser"});
    BOOST_REQUIRE_EQUAL(bare.size(), 1u);
    BOOST_CHECK(bare[0].error == wolf::Api::G2P::L1::Error::DriverUnavailable);
    BOOST_CHECK(bare[0].pronunciation.empty());
}

/// The same input ordering the other two variants owe. Here it also has to survive batching: the
/// backend decodes the convertible words as one request, so a ruled-on word must be taken out of
/// the batch and its result put back in the right slot.
BOOST_AUTO_TEST_CASE(test_MultiG2P_HonoursTheInputOrdering) {
    ds::InferenceDriverFactory factory;
    srt::SynthUnit unit;
    configure(unit);
    BOOST_REQUIRE(registerDriver(factory, unit));

    const auto words = convert(unit, "eng", {"hello", "", "two words", "world"});
    BOOST_REQUIRE_EQUAL(words.size(), 4u);

    BOOST_CHECK(words[0].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(!words[0].pronunciation.empty());

    BOOST_CHECK(words[1].mode == wolf::Api::G2P::L1::Mode::Skip);
    BOOST_CHECK(words[1].pronunciation.empty());

    BOOST_CHECK(words[2].error == wolf::Api::G2P::L1::Error::InvalidInput);
    BOOST_CHECK_EQUAL(words[2].pronunciation, "two words");

    // The last word is the one a misplaced batch result would corrupt: two of the four never
    // reached the model, so the model's second answer belongs here and not one slot earlier.
    BOOST_CHECK(words[3].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(!words[3].pronunciation.empty());
    BOOST_CHECK(words[3].pronunciation != words[0].pronunciation);
}

/// Rehearses what an editor actually does, which is the case nothing else here covers.
///
/// synthrt deploys no ONNX Runtime of its own: the driver loads whichever directory the host names
/// in DriverInitArgs::runtimePath, and where that is is the application's decision. An editor
/// stages the payload into its own tree — not the vcpkg package it came from, and not beside the
/// driver plugin — and then says where it put it. This runs the whole chain that way and checks
/// the driver really loaded from there, so an accidental dependency on either of the other two
/// locations would fail rather than pass by coincidence.
BOOST_AUTO_TEST_CASE(test_MultiG2P_RunsOnARuntimeTheHostDeployed) {

    const auto staged = deployRuntimeLikeAHost();
    BOOST_TEST_MESSAGE("host deployed the runtime to " + staged.string());

    {
        ds::InferenceDriverFactory factory;
        srt::SynthUnit unit;
        configure(unit);
        BOOST_REQUIRE(registerDriver(factory, unit, staged));

        // The driver reports the library it actually opened; it has to be the one just deployed.
        auto *service = unit.runtimeService(ds::InferenceDriverPlugin::IID, OnnxApi::API_NAME);
        BOOST_REQUIRE(service != nullptr);
        const auto *extension = service->as<ds::InferenceDriver>()->extension();
        BOOST_REQUIRE(extension != nullptr);
        const auto loaded = extension->as<OnnxApi::DriverExtension>()->runtimePath;
        BOOST_CHECK_MESSAGE(loaded.parent_path() == staged, "the driver loaded " + loaded.string() +
                                                                " rather than one under " +
                                                                staged.string());

        const auto words = convert(unit, "eng", {"hello"});
        BOOST_REQUIRE_EQUAL(words.size(), 1u);
        BOOST_CHECK(words[0].error == wolf::Api::G2P::L1::Error::None);
        BOOST_CHECK(!words[0].pronunciation.empty());
    }

    std::error_code ec;
    fs::remove_all(staged, ec);
}

BOOST_AUTO_TEST_SUITE_END()
