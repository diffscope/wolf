#include <algorithm>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

BOOST_AUTO_TEST_SUITE(test_LinguistContrib)

BOOST_AUTO_TEST_CASE(test_LinguistContrib_Registered) {
    std::vector<std::string_view> names;
    for (const auto &entry : srt::ContribCategoryRegistry::entries()) {
        names.push_back(entry.name());
    }

    BOOST_CHECK(std::find(names.begin(), names.end(), wolf::LINGUIST_CATEGORY) != names.end());
}

BOOST_AUTO_TEST_CASE(test_LinguistContrib_BuiltByEveryUnit) {
    srt::SynthUnit unit;
    auto category = unit.category(wolf::LINGUIST_CATEGORY);
    BOOST_REQUIRE(category != nullptr);
    BOOST_CHECK_EQUAL(category->name(), wolf::LINGUIST_CATEGORY);
    BOOST_CHECK(&category->synthUnit() == &unit);
    BOOST_CHECK(category->as<wolf::LinguistCategory>()->linguists().empty());

    srt::SynthUnit other;
    BOOST_CHECK(other.category(wolf::LINGUIST_CATEGORY) != nullptr);
    BOOST_CHECK(other.category(wolf::LINGUIST_CATEGORY) != category);
}

BOOST_AUTO_TEST_SUITE_END()
