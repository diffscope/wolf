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

    /// Fails the current test with the error text if \a handle holds an error. Calling error() on
    /// an Expected that holds a value is undefined behavior; the check therefore precedes the
    /// construction of the message.
    void requireOpened(const srt::Expected<srt::PackageHandle> &handle, const char *what) {
        if (!handle) {
            BOOST_FAIL(std::string(what) + " should have opened: " + handle.error().toString());
        }
    }

    /// Opens the package at \a path in DataOnly mode. DataOnly stops after manifest construction and
    /// therefore exercises the parsing of the category without requiring any interpreter plugin.
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

    // The identity fields must be readable without any interpreter, which allows a host to list
    // the installed languages before loading any package.
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
    // substring pins the rejection reason, so that a fixture rejected for an unrelated reason does
    // not count as a pass.
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

/// Checks that an unknown field in the declaration root is ignored rather than rejected.
///
/// The root is an object defined by the upstream specification, which requires the framework to
/// validate only the fields that it defines and forbids rejection because of unknown fields. A
/// category that rejected unknown fields would cause every field that the framework adds later to
/// fail to load on wolf builds that predate the field. Such a forward-compatibility break is not
/// wolf's to impose on its upstream. wolf remains strict for the schemas that it owns: `exports`,
/// `configuration` and `imports[].options` all reject keys that they do not define.
///
/// No validation is lost. A misspelt *required* field still fails because the correctly spelt
/// field is then missing; the identity cases above cover that failure.
BOOST_AUTO_TEST_CASE(test_LinguistIdentity_IgnoresUnknownDeclarationField) {
    srt::SynthUnit unit;
    auto handle = openData(unit, fixturePath("unknown-field"));
    requireOpened(handle, "unknown-field");

    // The ignored field does not affect the identity fields beside it, which are still read.
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

    // A JSON object orders its members by key, so the map is read in key order rather than in
    // declaration order. For this reason, defaultLanguage must be explicit.
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

// The shape of the map belongs to the singer category, so synthrt rejects a malformed map when the
// package opens, before wolf reads the map. These cases verify that the fixtures used by the wolf
// tests are rejected for the reason that each fixture was written to trigger.
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
