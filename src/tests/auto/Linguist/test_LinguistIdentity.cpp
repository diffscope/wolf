#include <filesystem>
#include <string>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Linguist/SingerLanguages.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace {

    fs::path packagePath(const char *name) {
        return fs::path(WOLF_TEST_PACKAGE_DIR) / name;
    }

    fs::path fixturePath(const char *name) {
        return fs::path(WOLF_TEST_FIXTURE_DIR) / name;
    }

    /// Fails the current test with the error text. Reading error() from a successful Expected is
    /// undefined, so the check has to happen before the message is ever built.
    void requireOpened(const srt::Expected<srt::PackageHandle> &handle, const char *what) {
        if (!handle) {
            BOOST_FAIL(std::string(what) + " should have opened: " + handle.error().toString());
        }
    }

    /// Opens a package as data. DataOnly stops after manifest construction, so it exercises the
    /// category's parsing without needing any interpreter plugin to exist.
    srt::Expected<srt::PackageHandle> openData(srt::SynthUnit &unit, const fs::path &path) {
        return unit.openPackage(path, srt::SynthUnit::DataOnly);
    }

}

BOOST_AUTO_TEST_SUITE(test_LinguistIdentity)

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_ReadsLanguageAndScheme) {
    srt::SynthUnit unit;
    auto handle = openData(unit, packagePath("wolf-lang-zxx"));
    requireOpened(handle, "wolf/lang-zxx");

    BOOST_CHECK_EQUAL(handle->id(), "wolf/lang-zxx");

    auto *spec = handle->contribution(wolf::LINGUIST_CATEGORY, "zxx-passthrough");
    BOOST_REQUIRE(spec != nullptr);

    auto *linguist = spec->as<wolf::LinguistSpec>();
    BOOST_REQUIRE(linguist != nullptr);
    BOOST_CHECK_EQUAL(linguist->language(), "zxx");
    BOOST_CHECK_EQUAL(linguist->scheme(), "passthrough");

    // The identity面 must be readable without any interpreter, which is what lets a host list
    // installed languages before deciding to load anything.
    BOOST_CHECK(spec->exports() == nullptr);
}

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_AllowsManySchemesPerLanguage) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("multi-scheme"));
    requireOpened(handle, "multi-scheme");

    auto contributions = handle->contributions(wolf::LINGUIST_CATEGORY);
    BOOST_REQUIRE_EQUAL(contributions.size(), 2u);

    for (auto *spec : contributions) {
        BOOST_CHECK_EQUAL(spec->as<wolf::LinguistSpec>()->language(), "cmn");
    }
    BOOST_CHECK_EQUAL(handle->contribution(wolf::LINGUIST_CATEGORY, "cmn-pinyin")
                          ->as<wolf::LinguistSpec>()
                          ->scheme(),
                      "pinyin");
    BOOST_CHECK_EQUAL(handle->contribution(wolf::LINGUIST_CATEGORY, "cmn-bopomofo")
                          ->as<wolf::LinguistSpec>()
                          ->scheme(),
                      "bopomofo");
}

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_RejectsMalformedIdentity) {
    // Each fixture is a package whose only defect is the one named by its directory. The expected
    // substring pins why it was rejected, so a fixture that breaks for some unrelated reason does
    // not quietly count as a pass.
    const struct {
        const char *fixture;
        const char *reason;
    } cases[] = {
        {"bad-language", "language"},
        {"bad-scheme",   "scheme"  },
        {"no-language",  "language"},
        {"no-scheme",    "scheme"  },
    };
    for (const auto &entry : cases) {
        srt::SynthUnit unit;
        auto handle = openData(unit, fixturePath(entry.fixture));
        BOOST_REQUIRE_MESSAGE(!handle,
                              std::string("fixture should have been rejected: ") + entry.fixture);
        const auto message = handle.error().toString();
        BOOST_CHECK_MESSAGE(message.find(entry.reason) != std::string::npos,
                            std::string(entry.fixture) +
                                " failed for the wrong reason: " + message);
    }
}

/// An unknown field in the declaration root is ignored, not rejected.
///
/// The root is an object the upper specification defines, and it says of such objects that the
/// framework validates only the fields it knows and that unknown ones must not cause rejection.
/// A category that rejects them makes every field the framework adds later fail to load against
/// builds of wolf that predate it — a forward compatibility break wolf has no standing to impose
/// on its own upstream. wolf stays strict where it really does own the schema: `exports`,
/// `configuration` and `imports[].options` all still refuse a key they do not define.
///
/// Nothing is lost. A misspelt *required* field still fails, because the field it should have been
/// is then missing — which the identity cases above cover.
BOOST_AUTO_TEST_CASE(test_LinguistIdentity_IgnoresUnknownDeclarationField) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("unknown-field"));
    requireOpened(handle, "unknown-field");

    // Ignored means ignored: the identity fields beside it are still read.
    auto *spec = handle->contribution(wolf::LINGUIST_CATEGORY, "cmn-pinyin");
    BOOST_REQUIRE(spec != nullptr);
    const auto *linguist = spec->as<wolf::LinguistSpec>();
    BOOST_REQUIRE(linguist != nullptr);
    BOOST_CHECK_EQUAL(linguist->language(), "cmn");
    BOOST_CHECK_EQUAL(linguist->scheme(), "pinyin");
}

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_ReadsSingerLanguageMap) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("singer-ok"));
    requireOpened(handle, "singer-ok");

    auto *singer = handle->contribution("singer", "s");
    BOOST_REQUIRE(singer != nullptr);

    auto languages = wolf::readSingerLanguages(*singer);
    BOOST_REQUIRE_MESSAGE(static_cast<bool>(languages), "language map should have been read");
    BOOST_CHECK_EQUAL(languages->defaultLanguage, "cmn");
    BOOST_REQUIRE_EQUAL(languages->entries.size(), 2u);

    // A JSON object orders its members by key, so the map is read in that order rather than in
    // declaration order. That is exactly why defaultLanguage has to be explicit.
    BOOST_CHECK_EQUAL(languages->entries[0].language, "cmn");
    BOOST_CHECK_EQUAL(languages->entries[0].role, "lang/a");
    BOOST_CHECK_EQUAL(languages->entries[1].language, "jpn");
    BOOST_CHECK_EQUAL(languages->entries[1].role, "lang/b");
}

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_AcceptsSingerWithoutLanguages) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("singer-none"));
    requireOpened(handle, "singer-none");

    auto languages = wolf::readSingerLanguages(*handle->contribution("singer", "s"));
    BOOST_REQUIRE(static_cast<bool>(languages));
    BOOST_CHECK(languages->empty());
    BOOST_CHECK(languages->defaultLanguage.empty());
}

// The map's shape is the singer category's business, so a malformed one is refused by synthrt
// when the package opens, before wolf sees it. These cases pin that the fixtures wolf's own tests
// lean on are refused for the reason they were written to provoke.
BOOST_AUTO_TEST_CASE(test_LinguistIdentity_MalformedSingerLanguageMapDoesNotOpen) {
    const struct {
        const char *fixture;
        const char *reason;
    } cases[] = {
        {"singer-no-default",  "defaultLanguage"},
        {"singer-bad-role",    "does not exist" },
        {"singer-bad-default", "not one of"     },
        {"singer-bad-shape",   "must map"       },
    };
    for (const auto &entry : cases) {
        srt::SynthUnit unit;
        auto handle = openData(unit, fixturePath(entry.fixture));
        BOOST_REQUIRE_MESSAGE(!handle, std::string("should not have opened: ") + entry.fixture);
        const auto message = handle.error().toString();
        BOOST_CHECK_MESSAGE(message.find(entry.reason) != std::string::npos,
                            std::string(entry.fixture) +
                                " failed for the wrong reason: " + message);
    }
}

BOOST_AUTO_TEST_CASE(test_LinguistIdentity_LanguageMapIsRefusedOnOtherCategories) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("unknown-field"));
    requireOpened(handle, "unknown-field");
    auto *linguist = handle->contribution(wolf::LINGUIST_CATEGORY, "cmn-pinyin");
    BOOST_REQUIRE(linguist != nullptr);
    auto languages = wolf::readSingerLanguages(*linguist);
    BOOST_CHECK(!languages);
}

BOOST_AUTO_TEST_SUITE_END()
