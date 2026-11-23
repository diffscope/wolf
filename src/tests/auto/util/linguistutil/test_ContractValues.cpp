#include <string>
#include <vector>

#include <linguistutil/ContractValues.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(version_validation_distinguishes_invalid_from_unsupported) {
    auto valid = wolf::readFormatVersion(srt::JsonValue(1), 1);
    BOOST_REQUIRE(valid);
    BOOST_CHECK_EQUAL(valid.value(), 1);
    for (const auto &value : std::vector<srt::JsonValue>{srt::JsonValue(), 0, -1, true, "1", 1.5}) {
        auto refused = wolf::readFormatVersion(value, 1);
        BOOST_REQUIRE(!refused);
        BOOST_CHECK(refused.error().code() == srt::Error::InvalidFormat);
    }
    auto future = wolf::readFormatVersion(srt::JsonValue(2), 1);
    BOOST_REQUIRE(!future);
    BOOST_CHECK(future.error().code() == srt::Error::FeatureNotSupported);
}

BOOST_AUTO_TEST_CASE(language_map_preserves_order_and_runs_variant_validation) {
    const auto value = srt::JsonValue::fromJson(
        R"([{"language":"eng","scheme":"ipa","ref":"en"},{"language":"cmn","scheme":"pinyin","ref":"zh"}])",
        false);
    int calls = 0;
    auto map = wolf::LanguageMapEntry::readList(
        value,
        [&calls](const std::string &position, const std::string &ref) -> srt::Expected<void> {
            ++calls;
            BOOST_CHECK(!position.empty());
            BOOST_CHECK(!ref.empty());
            return {};
        });
    BOOST_REQUIRE(map);
    BOOST_REQUIRE_EQUAL(map.value().size(), 2u);
    BOOST_CHECK_EQUAL(map.value()[0].ref, "en");
    BOOST_CHECK_EQUAL(map.value()[1].ref, "zh");
    BOOST_CHECK_EQUAL(calls, 2);
    auto refused = wolf::LanguageMapEntry::readList(
        value, [](const std::string &, const std::string &) -> srt::Expected<void> {
            return srt::Error(srt::Error::FeatureNotSupported, "unknown engine");
        });
    BOOST_REQUIRE(!refused);
    BOOST_CHECK(refused.error().code() == srt::Error::FeatureNotSupported);
    for (
        const auto &text :
        {"[]", "{}", R"([{"language":"eng","scheme":"ipa","ref":""}])",
         R"([{"language":"eng","scheme":"ipa","ref":"x"},{"language":"eng","scheme":"ipa","ref":"y"}])"}) {
        auto invalid = wolf::LanguageMapEntry::readList(srt::JsonValue::fromJson(text, false));
        BOOST_REQUIRE(!invalid);
        BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
    }
}
