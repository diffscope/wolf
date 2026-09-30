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

    /// The directory of the converted packages. The shared backend contains 14 MiB of models and
    /// is therefore never committed; every test in this file requires it.
    using wolf::test::convertedRoot;

    /// Every test in this file requires the shared model backend package, the ONNX driver plugin
    /// and an ONNX Runtime to pass to the driver. If any of them is missing, the run is reported to
    /// ctest as a skip.
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

    /// Performs the host setup: finds the ONNX driver, initializes it with the runtime in
    /// \a runtimeDir, and registers it as the backend shared by the whole unit. A module never
    /// loads its own driver.
    ///
    /// \return \c true if the driver was registered; \c false if the driver plugin is missing or
    ///         the driver could not be created or initialized.
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

    /// Deploys ONNX Runtime as an editor does, into a directory chosen by the application and
    /// unrelated to both the package location and the driver plugin location, and returns that
    /// directory.
    ///
    /// Symbolic links are copied as symbolic links because the payload consists of them:
    /// libonnxruntime.so points to .so.1, which points to the real file, and dereferencing the
    /// links would deploy three copies.
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

/// Runs the shipped model end to end: a singer selects a language, the chain of that language
/// reaches the shared backend through an import, and the backend runs three ONNX models on a real
/// word.
BOOST_AUTO_TEST_CASE(test_MultiG2P_ConvertsThroughTheSharedModel) {
    // Declared before the unit because the factory holds the plugin code, and every driver created
    // from the factory must be destroyed first.
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
        // The contract family defines a pronunciation as space-delimited symbols.
        BOOST_CHECK_MESSAGE(word.pronunciation.find(' ') != std::string::npos,
                            "expected several symbols, got: " + word.pronunciation);
        // The phoneme layer splits on those spaces, so the two layers must agree.
        BOOST_CHECK(!word.phonemes.empty());
    }

    BOOST_TEST_MESSAGE("hello -> " + words[0].pronunciation);
    BOOST_TEST_MESSAGE("world -> " + words[1].pronunciation);

    // A word that this chain classifies as Copy never reaches the model.
    const auto punctuation = convert(unit, "eng", {","});
    BOOST_CHECK(punctuation[0].mode == wolf::Api::G2P::L1::Mode::Copy);
    BOOST_CHECK_EQUAL(punctuation[0].pronunciation, ",");
}

/// Serves several languages from one bundle in one process. The language ID derives from the
/// declaration order in the bundle, so an incorrect mapping would appear as the wrong phoneme set
/// rather than as an error.
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

    // The two languages use different phoneme sets, so the same spelling does not yield the same
    // reading. A swapped language ID fails this assertion.
    const auto englishWasser = convert(unit, "eng", {"wasser"});
    BOOST_CHECK_MESSAGE(englishWasser[0].pronunciation != german[0].pronunciation,
                        "both languages produced " + german[0].pronunciation +
                            ", which indicates that the language ID did not reach the model");
}

/// Without a driver, the package still loads and the chain still runs. The backend reports the
/// missing driver per word, which allows the fallback of the chain to take over.
BOOST_AUTO_TEST_CASE(test_MultiG2P_DegradesWithoutADriver) {
    srt::SynthUnit unit;
    configure(unit);
    // No driver is registered in this case.

    const auto covered = convert(unit, "eng", {"hello"});
    BOOST_REQUIRE_EQUAL(covered.size(), 1u);
    // The chain has a fallback, so the word is returned unchanged and counts as a success.
    BOOST_CHECK(covered[0].error == wolf::Api::G2P::L1::Error::None);
    BOOST_CHECK_EQUAL(covered[0].pronunciation, "hello");
    BOOST_CHECK(covered[0].hitStage == LinguistApi::HitStage::Fallback);

    // The chain of the other language has no fallback, so the error from the backend is retained.
    const auto bare = convert(unit, "deu", {"wasser"});
    BOOST_REQUIRE_EQUAL(bare.size(), 1u);
    BOOST_CHECK(bare[0].error == wolf::Api::G2P::L1::Error::DriverUnavailable);
    BOOST_CHECK(bare[0].pronunciation.empty());
}

/// Checks the input ordering that the other two variants also guarantee. This variant must
/// preserve the ordering under batching: the backend decodes the convertible words as one request,
/// so a word already decided by a rule must be removed from the batch and its result placed back
/// in the correct slot.
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

    // A misplaced batch result would corrupt the last word: two of the four words never reached
    // the model, so the second model result belongs in this slot and not one slot earlier.
    BOOST_CHECK(words[3].mode == wolf::Api::G2P::L1::Mode::Convert);
    BOOST_CHECK(!words[3].pronunciation.empty());
    BOOST_CHECK(words[3].pronunciation != words[0].pronunciation);
}

/// Reproduces the deployment performed by an editor, which no other case in this file covers.
///
/// synthrt does not deploy its own ONNX Runtime: the driver loads the directory specified by the
/// host in DriverInitArgs::runtimePath, and the application chooses that directory. An editor
/// stages the payload into its own tree, which is neither the source vcpkg package nor the driver
/// plugin directory, and then passes that location to the driver. This case runs the whole chain
/// in that configuration and verifies that the driver loaded the runtime from the staged
/// directory, so that an accidental dependency on either of the other two locations fails rather
/// than passes by coincidence.
BOOST_AUTO_TEST_CASE(test_MultiG2P_RunsOnARuntimeTheHostDeployed) {

    const auto staged = deployRuntimeLikeAHost();
    BOOST_TEST_MESSAGE("host deployed the runtime to " + staged.string());

    {
        ds::InferenceDriverFactory factory;
        srt::SynthUnit unit;
        configure(unit);
        BOOST_REQUIRE(registerDriver(factory, unit, staged));

        // The driver reports the library that it opened, which must be the library just deployed.
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
