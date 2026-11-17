#include <filesystem>
#include <fstream>
#include <string>

#include <multig2p/Bundle.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

using wolf::multig2p::Bundle;
using wolf::multig2p::Vocabulary;

namespace {

    /// Writes \a content to a scratch file and returns its path.
    fs::path writeJson(const std::string &name, const std::string &content) {
        const auto path = fs::temp_directory_path() / ("wolf-multig2p-" + name + ".json");
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }

    /// Loads \a content as a T and returns the error, failing the case if it loads.
    template <class T>
    srt::Error refusal(const std::string &name, const std::string &content) {
        const auto path = writeJson(name, content);
        auto loaded = T::load(path);
        fs::remove(path);
        BOOST_REQUIRE_MESSAGE(!loaded, name + " should be refused");
        return loaded.error();
    }

    void checkRefused(const srt::Error &error, srt::Error::ErrorCode code,
                      const std::string &part) {
        BOOST_CHECK(error.code() == code);
        BOOST_CHECK_MESSAGE(error.message().find(part) != std::string::npos, error.message());
    }

}

BOOST_AUTO_TEST_SUITE(test_Bundle)

/// The reserved symbols are found by name wherever the table puts them, and the ids of the
/// other symbols are their positions.
BOOST_AUTO_TEST_CASE(test_Bundle_LocatesReservedSymbolsByName) {
    const auto path = writeJson("vocab", R"({
    "symbols": ["zh/pinyin/a", "<pad>", "<unk>", "en/arpabet/hh", "<eos>", "<bos>"]
})");
    auto vocabulary = Vocabulary::load(path);
    fs::remove(path);
    BOOST_REQUIRE_MESSAGE(vocabulary.hasValue(),
                          (vocabulary ? std::string() : vocabulary.error().message()));

    BOOST_CHECK_EQUAL(vocabulary->size(), 6u);
    BOOST_CHECK_EQUAL(vocabulary->specials().padding, 1);
    BOOST_CHECK_EQUAL(vocabulary->specials().unknown, 2);
    BOOST_CHECK_EQUAL(vocabulary->specials().end, 4);
    BOOST_CHECK_EQUAL(vocabulary->specials().begin, 5);

    BOOST_CHECK_EQUAL(vocabulary->lookup("en/arpabet/hh"), 3);
    BOOST_CHECK_EQUAL(vocabulary->lookup("hh"), -1);
    BOOST_CHECK(!vocabulary->isSpecial(0));
    BOOST_CHECK(!vocabulary->isSpecial(3));
    for (const auto id : {1, 2, 4, 5}) {
        BOOST_CHECK(vocabulary->isSpecial(id));
    }

    // A phoneme is the symbol without its language and variant, and an id out of range has none
    BOOST_CHECK_EQUAL(vocabulary->phonemeAt(3), "hh");
    BOOST_CHECK_EQUAL(vocabulary->phonemeAt(1), "<pad>");
    BOOST_CHECK_EQUAL(vocabulary->phonemeAt(6), "");
    BOOST_CHECK_EQUAL(vocabulary->phonemeAt(-1), "");
}

BOOST_AUTO_TEST_CASE(test_Bundle_RefusesMalformedVocabularies) {
    checkRefused(
        refusal<Vocabulary>("vocab-repeat",
                            R"({"symbols": ["<pad>", "<unk>", "<bos>", "<eos>", "a", "a"]})"),
        srt::Error::InvalidFormat, "repeats a symbol: a");
    checkRefused(refusal<Vocabulary>("vocab-no-eos", R"({"symbols": ["<pad>", "<unk>", "<bos>"]})"),
                 srt::Error::InvalidFormat, "missing the reserved symbol <eos>");
    checkRefused(refusal<Vocabulary>("vocab-empty", R"({"symbols": []})"),
                 srt::Error::InvalidFormat, "holds no symbols");
    checkRefused(refusal<Vocabulary>("vocab-absent", R"({})"), srt::Error::InvalidFormat,
                 "needs symbols");
    checkRefused(refusal<Vocabulary>("vocab-not-array", R"({"symbols": "<pad>"})"),
                 srt::Error::InvalidFormat, "must be an array");
    checkRefused(refusal<Vocabulary>("vocab-empty-symbol", R"({"symbols": ["<pad>", ""]})"),
                 srt::Error::InvalidFormat, "non-empty strings");
    checkRefused(refusal<Vocabulary>("vocab-array", R"(["<pad>"])"), srt::Error::InvalidFormat,
                 "must hold an object");
    checkRefused(refusal<Vocabulary>("vocab-broken", R"({"symbols": [)"), srt::Error::InvalidFormat,
                 "is not valid JSON");
}

/// The language id is the position in the list, and the bundle names files rather than paths.
BOOST_AUTO_TEST_CASE(test_Bundle_ReadsFilesAndLanguages) {
    const auto path = writeJson("bundle", R"({
    "bundle_version": 1,
    "schema_version": 3,
    "files": { "encoder": "encoder.onnx", "decoder": "decoder.onnx" },
    "languages": ["zh", "en", "ja"]
})");
    auto bundle = Bundle::load(path);
    fs::remove(path);
    BOOST_REQUIRE_MESSAGE(bundle.hasValue(), (bundle ? std::string() : bundle.error().message()));

    auto encoder = bundle->fileName("encoder");
    BOOST_REQUIRE(encoder);
    BOOST_CHECK_EQUAL(encoder.value(), "encoder.onnx");
    auto missing = bundle->fileName("vocoder");
    BOOST_REQUIRE(!missing);
    checkRefused(missing.error(), srt::Error::InvalidFormat, "does not name a vocoder model");

    BOOST_CHECK_EQUAL(bundle->languages().size(), 3u);
    BOOST_CHECK_EQUAL(bundle->languageId("zh"), 0);
    BOOST_CHECK_EQUAL(bundle->languageId("ja"), 2);
    BOOST_CHECK_EQUAL(bundle->languageId("ko"), -1);
}

/// A version above the one this build reads is not a malformed file but a newer one, and the
/// error says so.
BOOST_AUTO_TEST_CASE(test_Bundle_RefusesVersionsItCannotRead) {
    const std::string rest = R"("files": {"encoder": "e.onnx"}, "languages": ["zh"])";
    checkRefused(refusal<Bundle>("bundle-newer", R"({"bundle_version": 2, )" + rest + "}"),
                 srt::Error::FeatureNotSupported, "upgrade wolf");
    for (const auto &version : {"0", "-1", "\"1\""}) {
        checkRefused(
            refusal<Bundle>("bundle-version",
                            R"({"bundle_version": )" + std::string(version) + ", " + rest + "}"),
            srt::Error::InvalidFormat, "positive integer bundle_version");
    }
    checkRefused(refusal<Bundle>("bundle-unversioned", "{" + rest + "}"), srt::Error::InvalidFormat,
                 "positive integer bundle_version");
}

BOOST_AUTO_TEST_CASE(test_Bundle_RefusesMalformedBundles) {
    checkRefused(
        refusal<Bundle>("bundle-no-files", R"({"bundle_version": 1, "languages": ["zh"]})"),
        srt::Error::InvalidFormat, "needs a files object");
    checkRefused(refusal<Bundle>("bundle-empty-file",
                                 R"({"bundle_version": 1, "files": {"encoder": ""},
                                     "languages": ["zh"]})"),
                 srt::Error::InvalidFormat, "encoder must be a non-empty file name");
    checkRefused(refusal<Bundle>("bundle-no-languages",
                                 R"({"bundle_version": 1, "files": {"encoder": "e.onnx"}})"),
                 srt::Error::InvalidFormat, "needs a languages list");
    checkRefused(refusal<Bundle>("bundle-empty-languages",
                                 R"({"bundle_version": 1, "files": {"encoder": "e.onnx"},
                                     "languages": []})"),
                 srt::Error::InvalidFormat, "lists no languages");
}

BOOST_AUTO_TEST_SUITE_END()
