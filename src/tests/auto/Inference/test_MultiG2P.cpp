#include <filesystem>
#include <fstream>
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
namespace G2PApi = wolf::Api::G2P::L1;
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
        wolf::test::configure(unit, {wolf::test::fixtureRoot(), convertedRoot()});
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
        auto handle = unit.openPackage(wolf::test::fixtureRoot() / "singer-model",
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

    /// The directory name of the shared backend under the converted root, and the two declarations
    /// of the model fixture that reach it.
    constexpr char BACKEND[] = "wolf-g2p-multi";
    constexpr char CHAIN[] = "chain";
    constexpr char STRICT[] = "strict";

    /// The language pair that the model-step cases bind. The bundle of the shared backend maps
    /// eng/arpabet and deu/ds, so this pair reaches the branch in which the backend of a model step
    /// cannot be built. Every pair that a fixture declares is mapped, which is why these cases
    /// choose a pair of their own instead of reusing one.
    constexpr char UNMAPPED_LANGUAGE[] = "eng";
    constexpr char UNMAPPED_SCHEME[] = "unmapped";

    /// One chain executive and the loaded package that owns its declaration.
    ///
    /// The handle is held rather than dropped: the executive refers to the declaration of that
    /// package, and the last handle is what keeps the package alive. The member order destroys the
    /// executive first.
    struct BoundChain {
        srt::PackageHandle package;
        std::unique_ptr<srt::InferenceExecutive> executive;

        /// Returns the contract type of the executive, which is what start() and state() need.
        G2PApi::G2PExecutive *get() const {
            return static_cast<G2PApi::G2PExecutive *>(executive.get());
        }
    };

    /// Binds the inference contribution \a id of the package at \a directory to \a language and
    /// \a scheme, and returns the executive together with its package.
    ///
    /// The cases below need a binding that the shared backend does not serve, and the route through
    /// the singer cannot produce one: a linguist passes its own pair down, and every pair of the
    /// fixtures is mapped by the bundle. Binding the contribution directly is what a host does, and
    /// the max-length case binds the backend the same way.
    BoundChain bindChain(srt::SynthUnit &unit, const fs::path &directory, const char *id,
                         const std::string &language, const std::string &scheme) {
        auto handle = unit.openPackage(directory, srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the package should have loaded: " + handle.error().toString());
        }
        auto *contribution = handle->contribution("inference", id);
        BOOST_REQUIRE(contribution != nullptr);
        auto *spec = contribution->as<srt::InferenceSpec>();
        BOOST_REQUIRE(spec != nullptr);

        G2PApi::G2PImportOptions importOptions(spec->variant());
        G2PApi::G2PRuntimeOptions runtimeOptions(spec->variant());
        runtimeOptions.binding = {language, scheme};
        auto executive = spec->createInference(importOptions, runtimeOptions);
        if (!executive) {
            BOOST_FAIL("the executive should have been created: " + executive.error().toString());
        }
        return BoundChain{handle.take(), executive.take()};
    }

    /// Checks that \a error is the refusal of a model step whose backend could not be built: the
    /// chain names the step, and the cause names the pair that no language map covers.
    ///
    /// Both halves are checked together, because the pair alone could also appear in a per-word
    /// result if the failure of the backend were still written into the words of the batch.
    void checkRefused(const srt::Error &error) {
        const auto message = error.toString();
        BOOST_CHECK_MESSAGE(message.find(R"(model step "backend" could not be built)") !=
                                std::string::npos,
                            message);
        const auto cause =
            std::string("maps no language to ") + UNMAPPED_LANGUAGE + "/" + UNMAPPED_SCHEME;
        BOOST_CHECK_MESSAGE(message.find(cause) != std::string::npos, message);
    }

    /// Writes a package whose chain classifies the words, looks them up in a dictionary and then
    /// reaches the shared backend through a model step, and returns its directory.
    ///
    /// The declaration is written rather than taken from a fixture, because no fixture holds a
    /// dictionary step before a model step, and the cases that use this one vary exactly what that
    /// dictionary leaves for the model. Its exports are those of the English fixture, while the
    /// cases bind the pair that no bundle serves: as with the fixture chains, the pair that a
    /// language asks its backend for is the subject, not the pair the declaration advertises.
    ///
    /// \a name selects the temporary directory, so that the cases of this file run in any order and
    /// repeat without a previous run interfering.
    fs::path writeDictChain(const std::string &name) {
        const auto root = fs::temp_directory_path() / ("wolf-chain-dict-" + name);
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(root / "inferences" / "g2p", error);
        BOOST_REQUIRE_MESSAGE(!error,
                              "the temporary package should have been created: " + error.message());

        std::ofstream(root / "desc.json") << R"({
    "$version": "1.0",
    "id": "wolf/test-lang-model-dict",
    "version": "1.0.0.0",
    "runtimeLevel": 1,
    "contributions": {
        "inference": [ { "id": "g2p", "path": "./inferences/g2p/inference.json" } ]
    },
    "dependencies": [
        { "id": "wolf/g2p-multi", "version": "1.0.0.0" }
    ]
})";
        // The dictionary covers "a" and not "b", which is what separates the two cases below.
        std::ofstream(root / "inferences" / "g2p" / "dict.txt") << "a\tp q\n";
        // The delimiter is a word rather than empty because the declaration holds ")" sequences: a
        // plain R"( ... )" literal would end in the middle of it.
        std::ofstream(root / "inferences" / "g2p" / "inference.json") << R"json({
    "interface": "org.openvpi.wolf.inference.G2P",
    "level": 1,
    "variant": "pipe-chain",
    "exports": {
        "languages": [ { "language": "eng", "scheme": "arpabet" } ]
    },
    "configuration": {
        "formatVersion": 1,
        "steps": [
            {
                "step": "verify",
                "params": {
                    "entries": [
                        { "type": "regex", "value": ["([A-Za-z]+)"], "mode": "convert" }
                    ]
                }
            },
            { "step": "dict", "params": { "file": "./dict.txt" } },
            { "step": "model", "params": { "role": "backend" } },
            { "step": "fallback", "params": { "useOriginal": true } }
        ]
    },
    "imports": [
        { "role": "backend", "ref": "wolf/g2p-multi:inference/multig2p" }
    ]
})json";
        return root;
    }

    /// Returns the directory of a private copy of the converted backend package, so that a case can
    /// remove one of the files that its bundle names without touching the generated tree that other
    /// tests share.
    ///
    /// A copy is required rather than a reference: the models are opened relative to the directory
    /// of the declaration, so a bundle alone would still name the models of the original package.
    ///
    /// The copy is made entry by entry rather than with one recursive copy, whose behavior on an
    /// existing destination directory differs between implementations. A link is refused instead of
    /// copied, because copying only the link or only an empty directory of that name would produce
    /// a package that is not the one the case means to test.
    ///
    /// \a name selects the temporary directory, so that the cases of this file run in any order and
    /// repeat without a previous run interfering.
    fs::path copiedBackend(const std::string &name) {
        const auto source = convertedRoot() / BACKEND;
        const auto root = fs::temp_directory_path() / ("wolf-multig2p-verify-" + name);

        std::error_code error;
        fs::remove_all(root, error);
        BOOST_REQUIRE_MESSAGE(!error, "the stale temporary root should have been removed: " +
                                          error.message());
        fs::create_directories(root, error);
        BOOST_REQUIRE_MESSAGE(!error,
                              "the temporary root should have been created: " + error.message());
        for (const auto &entry : fs::recursive_directory_iterator(source)) {
            const auto relative = entry.path().lexically_relative(source);
            BOOST_REQUIRE_MESSAGE(!entry.is_symlink(), "the package should hold no link to copy: " +
                                                           entry.path().string());
            if (entry.is_directory()) {
                fs::create_directories(root / relative, error);
            } else {
                fs::copy_file(entry.path(), root / relative, fs::copy_options::overwrite_existing,
                              error);
            }
            BOOST_REQUIRE_MESSAGE(!error,
                                  "the package should have been copied: " + error.message());
        }
        return root;
    }

    /// Removes the temporary tree of a case that reached its end, so that a passing run leaves no
    /// copy of the models behind. The tree is not the evidence of a failure: every check of a case
    /// carries the readings it compares, and a case that aborts before its end keeps its tree at
    /// the path that its helper derived from the name of the case.
    void discard(const fs::path &root) {
        std::error_code error;
        fs::remove_all(root, error);
        if (error) {
            BOOST_TEST_MESSAGE("the temporary tree was kept: " + root.string() + ": " +
                               error.message());
        }
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

/// A chain with a fallback step must not hide a model step whose backend cannot be built: the batch
/// fails as a whole, and the caller receives no result at all.
///
/// Before this rule the step wrote a per-word failure into the words it could not convert, and the
/// fallback step below it rewrote those failures into successes: the conversion reported the text
/// of every word as a pronunciation, so a check on the pronunciation passed while the model had
/// never run. The call itself must therefore be refused.
BOOST_AUTO_TEST_CASE(test_MultiG2P_FailsTheBatchWhenTheBackendCannotBeBuilt) {
    srt::SynthUnit unit;
    configure(unit);
    // No ONNX driver is registered: the backend of this case is refused while it is being built,
    // which happens before any session would be created.
    auto chain = bindChain(unit, wolf::test::fixtureRoot() / "lang-model", CHAIN, UNMAPPED_LANGUAGE,
                           UNMAPPED_SCHEME);

    G2PApi::G2PStartInput input;
    input.lyrics = {"hello"};
    auto result = chain.get()->start(input);

    BOOST_REQUIRE_MESSAGE(!result,
                          "the batch should have been refused: " + wolf::test::why(result));
    checkRefused(result.error());
    BOOST_CHECK(chain.get()->state() == srt::ITask::Failed);
}

/// The same refusal reaches a chain without a fallback step as an error of the batch rather than as
/// an error of the words: the words that the backend never saw are not returned at all.
///
/// The German fixture chain holds a model step and no fallback step, so before this rule the batch
/// reported success with every word carrying ModelInferenceFailed and no pronunciation.
BOOST_AUTO_TEST_CASE(test_MultiG2P_RefusesTheBatchWithoutAFallbackToo) {
    srt::SynthUnit unit;
    configure(unit);
    auto chain = bindChain(unit, wolf::test::fixtureRoot() / "lang-model", STRICT,
                           UNMAPPED_LANGUAGE, UNMAPPED_SCHEME);

    G2PApi::G2PStartInput input;
    input.lyrics = {"wasser"};
    auto result = chain.get()->start(input);

    BOOST_REQUIRE_MESSAGE(!result,
                          "the batch should have been refused: " + wolf::test::why(result));
    checkRefused(result.error());
    BOOST_CHECK(chain.get()->state() == srt::ITask::Failed);
}

/// A batch whose words the input ordering already settled leaves the model step nothing to convert,
/// so a backend that cannot be built is no failure at all: the task succeeds and every word keeps
/// the verdict of the ordering.
///
/// This case uses the chain and the pair of the first case above; only the words differ, which
/// isolates the work of the step from the state of its backend.
BOOST_AUTO_TEST_CASE(test_MultiG2P_SucceedsWhenNoWordNeedsTheBackend) {
    srt::SynthUnit unit;
    configure(unit);
    auto chain = bindChain(unit, wolf::test::fixtureRoot() / "lang-model", CHAIN, UNMAPPED_LANGUAGE,
                           UNMAPPED_SCHEME);

    G2PApi::G2PStartInput input;
    input.lyrics = {"", "   ", "two words"};
    auto result = chain.get()->start(input);

    BOOST_REQUIRE_MESSAGE(result, "the batch should have run: " + wolf::test::why(result));
    BOOST_CHECK(chain.get()->state() == srt::ITask::Succeeded);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 3u);

    // The verdicts of the input ordering, as test_PipeChain_HonoursTheInputOrdering expects them
    // for the same lyrics.
    for (const auto index : {0u, 1u}) {
        BOOST_CHECK((*result)->words[index].mode == G2PApi::Mode::Skip);
        BOOST_CHECK((*result)->words[index].error == G2PApi::Error::None);
        BOOST_CHECK((*result)->words[index].pronunciation.empty());
        BOOST_CHECK((*result)->words[index].candidates.empty());
    }
    BOOST_CHECK((*result)->words[2].error == G2PApi::Error::InvalidInput);
    BOOST_CHECK_EQUAL((*result)->words[2].pronunciation, "two words");
}

/// A dictionary step that covered every word leaves the model step no work, so the step reports no
/// failure and the words keep the reading the dictionary gave them.
///
/// This case pins one side of the boundary: the step asks for its backend before it looks at its
/// words, so the decision to report a failure must be taken from the words and not from the
/// backend.
BOOST_AUTO_TEST_CASE(test_MultiG2P_SucceedsWhenTheDictionaryCoveredEveryWord) {
    const auto root = writeDictChain("covered");
    srt::SynthUnit unit;
    configure(unit);
    auto chain = bindChain(unit, root, "g2p", UNMAPPED_LANGUAGE, UNMAPPED_SCHEME);

    G2PApi::G2PStartInput input;
    input.lyrics = {"a"};
    auto result = chain.get()->start(input);

    BOOST_REQUIRE_MESSAGE(result, "the batch should have run: " + wolf::test::why(result));
    BOOST_CHECK(chain.get()->state() == srt::ITask::Succeeded);
    BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
    BOOST_CHECK((*result)->words[0].error == G2PApi::Error::None);
    BOOST_CHECK((*result)->words[0].hitSource == G2PApi::HitSource::Dict);
    BOOST_CHECK_EQUAL((*result)->words[0].pronunciation, "p q");

    discard(root);
}

/// One word that the dictionary missed is enough to refuse the whole batch: the model step has work
/// to do, its backend cannot be built, and the caller receives no result at all, not even the
/// dictionary reading of the word that was covered.
///
/// This case pins the other side of the boundary of the case above. Before this rule the batch
/// succeeded with "a" read by the dictionary and "b" filled in by the fallback, so no single word
/// of the result would have distinguished the two behaviors.
BOOST_AUTO_TEST_CASE(test_MultiG2P_FailsTheBatchForOneWordTheDictionaryMissed) {
    const auto root = writeDictChain("missed");
    srt::SynthUnit unit;
    configure(unit);
    auto chain = bindChain(unit, root, "g2p", UNMAPPED_LANGUAGE, UNMAPPED_SCHEME);

    G2PApi::G2PStartInput input;
    input.lyrics = {"a", "b"};
    auto result = chain.get()->start(input);

    BOOST_REQUIRE_MESSAGE(!result,
                          "the batch should have been refused: " + wolf::test::why(result));
    checkRefused(result.error());
    BOOST_CHECK(chain.get()->state() == srt::ITask::Failed);

    discard(root);
}

/// A bundle whose models are missing is refused when the package is loaded, rather than accepted
/// and then failing word by word for every language that the bundle serves.
///
/// The check reads the files that the bundle names and creates no session, so it holds in a process
/// without a driver. Every model of the bundle is walked, because the refusal has to name the model
/// that is missing: what a user restores is that file, not a set of them.
BOOST_AUTO_TEST_CASE(test_MultiG2P_RejectsAPackageWhoseModelIsMissing) {
    const auto root = copiedBackend("model-missing");
    const auto directory = root / "inferences" / "multig2p";

    // Finds the file that the fixture records for a logical model name. decoder_step is a prefix of
    // decoder_step_init, so the longer name is excluded rather than matched by chance.
    const auto modelFile = [&directory](const std::string &logical) {
        fs::path found;
        for (const auto &entry : fs::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            if (entry.path().extension() != ".onnx" ||
                name.compare(0, logical.size(), logical) != 0)
                continue;
            // The name has to end at a boundary, so that a longer name is not matched as a prefix.
            if (name.size() > logical.size() && name.at(logical.size()) != '_')
                continue;
            if (logical == "decoder_step" && name.find("init") != std::string::npos)
                continue;
            BOOST_REQUIRE_MESSAGE(found.empty(), "the fixture should record one file per model");
            found = entry.path();
        }
        return found;
    };

    // The same copy loads while every file is present, so the refusal below cannot be an old
    // outcome of a load of another tree: the loader holds no result across loads. A unit resolves
    // one package per identity, so each load uses its own unit.
    {
        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        auto loaded = unit.openPackage(root, srt::SynthUnit::Load);
        BOOST_REQUIRE_MESSAGE(loaded, "the copy should load while its models are present: " +
                                          wolf::test::why(loaded));
    }

    // The model names are walked in a fixed order, so a failure names the model that was hidden.
    for (const auto *logical : {"encoder", "decoder_step_init", "decoder_step"}) {
        const auto model = modelFile(logical);
        BOOST_REQUIRE_MESSAGE(!model.empty(), std::string("the bundle should name a ") + logical +
                                                  " model");

        // Hidden rather than removed, so that the next name can be walked on the same tree.
        const auto hidden = fs::path(model.string() + ".hidden");
        std::error_code error;
        fs::rename(model, hidden, error);
        BOOST_REQUIRE_MESSAGE(!error, "the model should have been hidden: " + error.message());

        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        auto refused = unit.openPackage(root, srt::SynthUnit::Load);
        BOOST_REQUIRE_MESSAGE(!refused, std::string("a package whose ") + logical +
                                            " model is missing should have been refused");
        const auto message = refused.error().toString();
        BOOST_CHECK_MESSAGE(message.find(std::string("the bundle's ") + logical +
                                         " model is missing") != std::string::npos,
                            message);
        // The text names the file, which is what tells a user which model to restore.
        BOOST_CHECK_MESSAGE(message.find(model.filename().string()) != std::string::npos, message);
        // The load may add its own framing around the error of the contribution, so the code is read
        // from the innermost error of the chain, which is the one the check sets.
        BOOST_CHECK_MESSAGE(refused.error().rootCause().code() == srt::Error::FileNotFound,
                            "the refusal should report FileNotFound: " + message);

        fs::rename(hidden, model, error);
        BOOST_REQUIRE_MESSAGE(!error, "the model should have been restored: " + error.message());
    }

    // Every model is back in place, so the tree loads again: a refusal leaves no state behind.
    {
        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        auto loaded = unit.openPackage(root, srt::SynthUnit::Load);
        BOOST_REQUIRE_MESSAGE(loaded, "the copy should load again once its models are restored: " +
                                          wolf::test::why(loaded));
    }

    discard(root);
}

BOOST_AUTO_TEST_SUITE_END()
