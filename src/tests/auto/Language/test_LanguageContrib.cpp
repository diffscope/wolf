#include <algorithm>
#include <string_view>
#include <vector>

#include <synthrt/Core/Contribute.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Language/LanguageContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using srt::SynthUnit;
using wolf::LanguageCategory;

BOOST_AUTO_TEST_SUITE(test_LanguageContrib)

// The category wolf defines has to reach the registry synthrt reads.
//
// wolf is linked, so its registration runs before main and before any unit exists. That timing is
// the whole reason a library can contribute a category where a plugin cannot.
BOOST_AUTO_TEST_CASE(test_LanguageContrib_Registered) {
    std::vector<std::string_view> names;
    for (const auto &entry : srt::ContribCategoryRegistry::entries()) {
        names.push_back(entry.name());
    }

    BOOST_CHECK(std::find(names.begin(), names.end(), "language") != names.end());

    // synthrt's own categories are still there.
    BOOST_CHECK(std::find(names.begin(), names.end(), "singer") != names.end());
    BOOST_CHECK(std::find(names.begin(), names.end(), "inference") != names.end());
}

// Every unit builds a language category of its own.
BOOST_AUTO_TEST_CASE(test_LanguageContrib_BuiltByEveryUnit) {
    SynthUnit su;

    auto category = su.category("language");
    BOOST_REQUIRE(category != nullptr);
    BOOST_CHECK(category->name() == "language");
    BOOST_CHECK(category->SU() == &su);

    auto language = category->as<LanguageCategory>();
    BOOST_REQUIRE(language != nullptr);
    BOOST_CHECK(language->languages().empty());
    BOOST_CHECK(
        language->findLanguages(srt::ContribLocator("vendor/pkg", {}, "language", "any")).empty());

    SynthUnit other;
    BOOST_CHECK(other.category("language") != nullptr);
    BOOST_CHECK(other.category("language") != category);
}

// A language is referred to the same way every other contribute is.
BOOST_AUTO_TEST_CASE(test_LanguageContrib_Reference) {
    auto loc = srt::ContribLocator::fromString("vendor/pkg=1.0.0:language/mandarin");

    BOOST_CHECK(loc.package() == "vendor/pkg");
    BOOST_CHECK(loc.category() == "language");
    BOOST_CHECK(loc.id() == "mandarin");

    // Trailing zeros are dropped on the way back out, so the rendering is not the input.
    BOOST_CHECK(loc.toString() == "vendor/pkg=1.0:language/mandarin");
    BOOST_CHECK(srt::ContribLocator::fromString(loc.toString()) == loc);
}

BOOST_AUTO_TEST_SUITE_END()
