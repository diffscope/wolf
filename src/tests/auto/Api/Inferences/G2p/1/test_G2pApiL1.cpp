#include <utility>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = wolf::Api::G2p::L1;

BOOST_AUTO_TEST_CASE(contract_identity_and_task_identity_are_preserved) {
    Api::Exports schema{};
    Api::ImportOptions options{};
    Api::RuntimeOptions runtime{};
    const srt::ContribSpecPayload *specPayloads[] = {&schema, &options, &runtime};
    for (const auto payload : specPayloads) {
        BOOST_CHECK(payload->contract() == Api::Contract);
    }
    Api::InitArgs args;
    BOOST_CHECK(args.contract() == Api::Contract);
    Api::StartInput input;
    Api::Result result;
    BOOST_CHECK(input.contract() == Api::Contract);
    BOOST_CHECK(result.contract() == Api::Contract);
    auto moved = std::move(input);
    BOOST_CHECK(moved.contract() == Api::Contract);
}
