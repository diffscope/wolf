#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;
namespace OnnxApi = ds::Api::Onnx;

/// Exercises the decode limit of the shared backend, which no other test covers: every other case
/// runs the declaration as converted, so a limit that was ignored, narrowed or applied to the wrong
/// loop would go unnoticed.
///
/// The backend is bound directly instead of through a chain, as a host may do, because the limit
/// belongs to the backend declaration rather than to any language that imports it.
namespace {

    /// The directory name of the shared backend under the converted root.
    constexpr char BACKEND[] = "wolf-g2p-multi";

    /// The declaration of the backend. Every case reads or rewrites this file inside its own copy.
    constexpr char DECLARATION[] = "inferences/multig2p/inference.json";

    /// The language pair that the cases bind: the bundle maps it to the English model, whose
    /// reading of the probe word is long enough for a limit of two symbols to truncate.
    constexpr char LANGUAGE[] = "eng";
    constexpr char SCHEME[] = "arpabet";

    /// The probe word and the limit that the cases apply to it.
    constexpr char WORD[] = "hello";
    constexpr int LIMIT = 2;

    /// Every case in this file needs the converted backend package, the ONNX driver plugin and an
    /// ONNX Runtime to pass to the driver. If any of them is missing, the run is reported to ctest
    /// as a skip rather than as a pass.
    struct DataOrSkip {
        DataOrSkip() {
            if (!fs::is_directory(wolf::test::convertedRoot() / BACKEND)) {
                wolf::test::skip("no converted backend package; set WOLF_LANG_PACKAGES_SOURCE "
                                 "or run scripts/convert-g2p-packages.py");
            }
            if (!fs::is_directory(fs::path(WOLF_TEST_DRIVER_PLUGIN_DIR)) ||
                std::string_view(WOLF_TEST_ONNXRUNTIME_DIR).empty()) {
                wolf::test::skip("no ONNX driver plugin or ONNX Runtime in this tree");
            }
        }
    };

    /// Creates the driver of the ONNX Runtime of this tree and registers it on \a unit as a host
    /// does, because the backend looks the service up instead of loading a driver itself.
    ///
    /// The registration is repeated here rather than shared with the chain test: the two files
    /// assemble the host differently, and a case that cannot be read on its own is worse than the
    /// thirty lines it would save.
    void registerDriver(ds::InferenceDriverFactory &factory, srt::SynthUnit &unit) {
        std::vector<fs::path> paths = {fs::path(WOLF_TEST_DRIVER_PLUGIN_DIR)};
        factory.setPluginPaths(paths);
        auto *loader = factory.find(OnnxApi::API_NAME);
        if (loader == nullptr) {
            BOOST_FAIL("the ONNX driver plugin should be present in this tree");
        }
        auto created = factory.create(loader);
        if (!created) {
            BOOST_FAIL("the driver should have been created: " + wolf::test::why(created));
        }
        auto driver = created.take();

        OnnxApi::DriverInitArgs args;
        args.ep = OnnxApi::ExecutionProvider::CPU;
        args.runtimePath = fs::path(WOLF_TEST_ONNXRUNTIME_DIR);
        if (auto ready = driver->initialize(args); !ready) {
            BOOST_FAIL("the driver should have been initialized: " + wolf::test::why(ready));
        }
        auto added = unit.addRuntimeService(std::move(driver));
        BOOST_REQUIRE_MESSAGE(added,
                              "the driver should have been registered: " + wolf::test::why(added));
    }

    /// Returns the directory of a private copy of the converted backend package, so that a case can
    /// rewrite its declaration without touching the generated tree that other tests share.
    ///
    /// A copy is required rather than a reference: the loader opens the models of the bundle
    /// relative to the directory of the declaration, so a patched declaration alone would still
    /// name the models of the original package.
    ///
    /// The copy is made entry by entry rather than with one recursive copy, whose behavior on an
    /// existing destination directory differs between implementations. A link is refused instead of
    /// copied: the iterator does not descend into a link to a directory, so copying only the link
    /// or only an empty directory of that name would produce a package that is not the one the
    /// case means to test.
    ///
    /// \a name selects the temporary directory, which lets the cases of this file run in any order
    /// and repeat without a previous run interfering.
    fs::path copiedBackend(const std::string &name) {
        const auto source = wolf::test::convertedRoot() / BACKEND;
        const auto root = fs::temp_directory_path() / ("wolf-multig2p-maxlen-" + name);

        std::error_code error;
        fs::remove_all(root, error);
        BOOST_REQUIRE_MESSAGE(!error, "the stale temporary root should have been removed: " +
                                          error.message());
        fs::create_directories(root, error);
        BOOST_REQUIRE_MESSAGE(!error, "the temporary root should have been created: " +
                                          error.message());
        for (const auto &entry : fs::recursive_directory_iterator(source)) {
            const auto relative = entry.path().lexically_relative(source);
            BOOST_REQUIRE_MESSAGE(!entry.is_symlink(),
                                  "the package should hold no link to copy: " +
                                      entry.path().string());
            if (entry.is_directory()) {
                fs::create_directories(root / relative, error);
            } else {
                fs::copy_file(entry.path(), root / relative,
                              fs::copy_options::overwrite_existing, error);
            }
            BOOST_REQUIRE_MESSAGE(!error,
                                  "the package should have been copied: " + error.message());
        }
        return root;
    }

    /// Rewrites the maxLen value of the declaration of the package at \a root.
    ///
    /// \a maxLenText is written as raw JSON text, so a case can write a valid limit, a zero and a
    /// string without this helper having to know which of them the declaration accepts. Only the
    /// value is replaced: the rest of the declaration, including the language map, stays as the
    /// converter wrote it.
    void setMaxLen(const fs::path &root, std::string_view maxLenText) {
        const auto declaration = root / DECLARATION;
        std::ifstream input(declaration, std::ios::binary);
        BOOST_REQUIRE_MESSAGE(input, "the declaration should have opened for reading: " +
                                         declaration.string());
        std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());

        constexpr std::string_view key = "\"maxLen\"";
        const auto keyPosition = text.find(key);
        BOOST_REQUIRE_MESSAGE(keyPosition != std::string::npos,
                              "the declaration should hold a maxLen key");
        const auto colon = text.find(':', keyPosition + key.size());
        BOOST_REQUIRE(colon != std::string::npos);
        const auto begin = text.find_first_not_of(" \t\r\n", colon + 1);
        // A value ends at the separator that follows it: a number is followed by a comma or by the
        // end of the object, and the closing quote of a string follows the string itself.
        const auto found = text.find_first_of(",}\r\n", begin);
        BOOST_REQUIRE(begin != std::string::npos && found != std::string::npos);
        auto end = found;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
            --end;
        }
        text.replace(begin, end - begin, maxLenText);

        std::ofstream output(declaration, std::ios::binary | std::ios::trunc);
        BOOST_REQUIRE_MESSAGE(output, "the declaration should have opened for writing");
        output << text;
        output.close();
        BOOST_REQUIRE_MESSAGE(output, "the declaration should have been written");
    }

    /// Removes the temporary copy of a case that reached its end, so that a passing run leaves no
    /// 19 MiB of models behind. The copy is not the evidence of a failure: every check of a case
    /// carries the readings it compares, and a case that aborts before its end leaves its copy at
    /// the path that copiedBackend() derives from the name of the case.
    void discard(const fs::path &root) {
        std::error_code error;
        fs::remove_all(root, error);
        if (error) {
            BOOST_TEST_MESSAGE("the temporary copy was kept: " + root.string() + ": " +
                               error.message());
        }
    }

    /// Runs the backend of \a backend on \a lyric and returns its reading.
    ///
    /// Every call builds its own unit, because a unit resolves one package per identity: a second
    /// load of the same identity would return the copy that was loaded first, and a case that
    /// compares two copies would then compare one of them with itself.
    std::string pronounce(const fs::path &backend, const std::string &lyric) {
        ds::InferenceDriverFactory factory;
        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        registerDriver(factory, unit);

        auto handle = unit.openPackage(backend, srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the backend should have loaded: " + handle.error().toString());
        }
        auto *contribution = handle->contribution("inference", "multig2p");
        BOOST_REQUIRE(contribution != nullptr);
        auto *spec = contribution->as<srt::InferenceSpec>();
        BOOST_REQUIRE(spec != nullptr);

        G2PApi::G2PImportOptions importOptions(spec->variant());
        G2PApi::G2PRuntimeOptions runtimeOptions(spec->variant());
        runtimeOptions.binding = {LANGUAGE, SCHEME};
        auto executive = spec->createInference(importOptions, runtimeOptions);
        if (!executive) {
            BOOST_FAIL("the executive should have been created: " + executive.error().toString());
        }

        G2PApi::G2PStartInput input;
        input.lyrics = {lyric};
        auto result = static_cast<G2PApi::G2PExecutive *>(executive->get())->start(input);
        if (!result) {
            BOOST_FAIL("the conversion should have run: " + result.error().toString());
        }
        BOOST_REQUIRE_EQUAL((*result)->words.size(), 1u);
        const auto &word = (*result)->words.front();
        BOOST_CHECK_MESSAGE(word.error == G2PApi::Error::None,
                            "the backend should have produced a reading");
        BOOST_CHECK(word.mode == G2PApi::Mode::Convert);
        return word.pronunciation;
    }

    /// Returns the number of symbols of \a pronunciation, which the contract defines as
    /// space-delimited.
    std::size_t symbolCount(const std::string &pronunciation) {
        return pronunciation.empty()
                   ? 0
                   : std::count(pronunciation.begin(), pronunciation.end(), ' ') + 1;
    }

}

BOOST_TEST_GLOBAL_FIXTURE(DataOrSkip);

BOOST_AUTO_TEST_SUITE(test_MultiG2PMaxLen)

/// A declaration may lower the number of decode steps, and the reading of a word must follow it.
BOOST_AUTO_TEST_CASE(test_MultiG2PMaxLen_CapsTheDecodedSymbolCount) {
    const auto capped = copiedBackend("capped");
    setMaxLen(capped, std::to_string(LIMIT));

    const auto full = pronounce(wolf::test::convertedRoot() / BACKEND, WORD);
    const auto limited = pronounce(capped, WORD);
    BOOST_TEST_MESSAGE(WORD + std::string(" at the converted limit -> ") + full);
    BOOST_TEST_MESSAGE(WORD + std::string(" at the reduced limit -> ") + limited);

    // The converted declaration must leave the word room, or the case below would pass without the
    // limit having any effect. Every check carries the readings, because the copy that produced
    // them is removed when the case reaches its end.
    BOOST_CHECK_MESSAGE(symbolCount(full) > static_cast<std::size_t>(LIMIT),
                        "the converted limit must allow more symbols than the reduced one: " +
                            full);
    BOOST_CHECK_MESSAGE(limited.size() < full.size(),
                        "the reduced limit must shorten the reading: " + limited + " against " +
                            full);
    BOOST_CHECK_MESSAGE(symbolCount(limited) <= static_cast<std::size_t>(LIMIT),
                        "the reduced limit must bound the number of symbols: " + limited);

    // The decode is greedy, so a lower limit truncates the same sequence instead of changing the
    // way it is produced: the symbols of the limited run are the first symbols of the full one.
    BOOST_CHECK_EQUAL(full.substr(0, limited.size()), limited);

    discard(capped);
}

/// The limit is checked where the declaration is interpreted, so a value that is not a positive
/// integer that fits an int makes the load fail instead of being narrowed to a limit that no
/// declaration asked for.
BOOST_AUTO_TEST_CASE(test_MultiG2PMaxLen_RejectsAValueOutsideAPositiveInt) {
    const auto patched = copiedBackend("rejected");
    constexpr char EXPECTED[] = "maxLen must be a positive integer that fits an int";

    // A valid limit on the same path loads, so that a rejection below cannot be a stale outcome of
    // an earlier load of that path: the loader holds no result across loads, and this holds it to
    // that as well. No driver is registered here, because interpreting a declaration opens no
    // model.
    setMaxLen(patched, std::to_string(LIMIT));
    {
        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        auto loaded = unit.openPackage(patched, srt::SynthUnit::Load);
        BOOST_REQUIRE_MESSAGE(loaded, "the copy should load with a valid limit: " +
                                          wolf::test::why(loaded));
    }

    for (const auto &text : {std::string("0"), std::string("-1"), std::string("2147483648"),
                             std::string("\"x\"")}) {
        setMaxLen(patched, text);

        srt::SynthUnit unit;
        wolf::test::configure(unit, {});
        auto handle = unit.openPackage(patched, srt::SynthUnit::Load);
        BOOST_REQUIRE_MESSAGE(!handle, "maxLen " + text + " should have been rejected");
        BOOST_CHECK_MESSAGE(handle.error().toString().find(EXPECTED) != std::string::npos,
                            "unexpected error for maxLen " + text + ": " +
                                handle.error().toString());
    }

    discard(patched);
}

BOOST_AUTO_TEST_SUITE_END()
