#include <algorithm>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribLocator.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Language/LanguageContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(test_LanguageContrib)

BOOST_AUTO_TEST_CASE(test_LanguageContrib_Registered) {
    std::vector<std::string_view> names;
    for (const auto &entry : srt::ContribCategoryRegistry::entries()) {
        names.push_back(entry.name());
    }

    BOOST_CHECK(std::find(names.begin(), names.end(), wolf::LANGUAGE_CATEGORY) != names.end());
}

BOOST_AUTO_TEST_CASE(test_LanguageContrib_BuiltByEveryUnit) {
    srt::SynthUnit unit;
    auto *category = unit.category(wolf::LANGUAGE_CATEGORY);
    BOOST_REQUIRE(category != nullptr);
    BOOST_CHECK_EQUAL(category->name(), wolf::LANGUAGE_CATEGORY);
    BOOST_CHECK(&category->synthUnit() == &unit);
    BOOST_CHECK(category->as<wolf::LanguageCategory>()->languages().empty());

    srt::SynthUnit other;
    BOOST_CHECK(other.category(wolf::LANGUAGE_CATEGORY) != nullptr);
    BOOST_CHECK(other.category(wolf::LANGUAGE_CATEGORY) != category);
}

BOOST_AUTO_TEST_CASE(test_LanguageContrib_Reference) {
    auto locator = srt::ContribLocator::fromString("vendor/pkg:org.openvpi.language/mandarin");

    BOOST_CHECK_EQUAL(locator.packageId(), "vendor/pkg");
    BOOST_CHECK_EQUAL(locator.category(), wolf::LANGUAGE_CATEGORY);
    BOOST_CHECK_EQUAL(locator.contributionId(), "mandarin");
    BOOST_CHECK_EQUAL(locator.toString(), "vendor/pkg:org.openvpi.language/mandarin");
}

BOOST_AUTO_TEST_SUITE_END()
