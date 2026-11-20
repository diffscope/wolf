#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>
#include <wolf/Api/Linguists/Linguist/1/LinguistWolfApiL1.h>

namespace Api = wolf::Api::Linguist::L1;

BOOST_AUTO_TEST_CASE(linguist_payloads_identify_one_fixed_contract) {
    Api::Exports exports;
    Api::Wolf::Configuration configuration;
    Api::ImportOptions options;
    Api::RuntimeOptions runtime;
    const srt::ContribSpecPayload *payloads[] = {&exports, &configuration, &options, &runtime};
    for (const auto payload : payloads)
        BOOST_CHECK(payload->contract() == Api::Contract);
    BOOST_CHECK(configuration.triplet() == Api::Wolf::Triplet);
    Api::ConvertInput input;
    Api::ConvertResult result;
    BOOST_CHECK(input.contract() == Api::Contract);
    BOOST_CHECK(result.contract() == Api::Contract);
}
