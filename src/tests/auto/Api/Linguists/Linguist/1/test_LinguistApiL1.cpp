#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = wolf::Api::Linguist::L1;

BOOST_AUTO_TEST_CASE(linguist_payloads_identify_one_fixed_contract) {
    Api::LinguistExports exports;
    Api::LinguistConfiguration configuration;
    Api::LinguistImportOptions options;
    Api::LinguistRuntimeOptions runtime;
    const srt::ContribSpecPayload *payloads[] = {&exports, &configuration, &options, &runtime};
    for (const auto payload : payloads)
        BOOST_CHECK(payload->triplet() ==
                    srt::ContribTriplet(Api::API_INTERFACE, Api::API_LEVEL, Api::API_VARIANT));
    Api::LinguistConvertInput input;
    Api::LinguistConvertResult result;
    BOOST_CHECK_EQUAL(input.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(result.version(), Api::API_LEVEL);
}
