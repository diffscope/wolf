#include <utility>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = wolf::Api::G2p::L1;

BOOST_AUTO_TEST_CASE(variant_identity_and_task_identity_are_separate) {
    Api::G2pExports schema("test.variant");
    Api::G2pImportOptions options("test.variant");
    Api::G2pRuntimeOptions runtime("test.variant");
    const srt::ContribSpecPayload *specPayloads[] = {&schema, &options, &runtime};
    for (const auto payload : specPayloads) {
        BOOST_CHECK(payload->triplet() == srt::ContribContractTriplet(
                                              Api::API_INTERFACE, Api::API_LEVEL, "test.variant"));
    }
    Api::G2pInitArgs args;
    BOOST_CHECK_EQUAL(args.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(args.version(), Api::API_LEVEL);
    Api::G2pStartInput input;
    Api::G2pResult result;
    BOOST_CHECK_EQUAL(input.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(input.version(), Api::API_LEVEL);
    BOOST_CHECK_EQUAL(result.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(result.version(), Api::API_LEVEL);
    auto moved = std::move(input);
    BOOST_CHECK_EQUAL(moved.type(), Api::API_INTERFACE);
}
