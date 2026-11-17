#define BOOST_TEST_MAIN
#include "TestSupport.h"

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>

BOOST_AUTO_TEST_CASE(provider_accepts_only_inference_role_targets_of_the_expected_contract) {
    const srt::ContribLocator inference("wolf/test", srt::InferenceCategory::NAME, "g2p");
    const srt::ContribLocator singer("wolf/test", srt::SingerCategory::NAME, "s");
    const auto &contract = wolf::Api::G2p::L1::Contract;
    const srt::ContribContractTriplet current(contract, "pipe-chain");
    const srt::ContribContractTriplet later(contract.interfaceId(), contract.level() + 1,
                                            "pipe-chain");

    BOOST_CHECK(wolf::detail::isCompatibleRoleTarget(inference, current, contract));
    BOOST_CHECK(!wolf::detail::isCompatibleRoleTarget(inference, later, contract));
    BOOST_CHECK(!wolf::detail::isCompatibleRoleTarget(singer, current, contract));
}

BOOST_AUTO_TEST_CASE(provider_parses_typed_contract_and_rejects_nonempty_import_options) {
    LoadedTree tree;
    wolf::WolfLinguistProvider provider;
    auto &spec = tree.linguist();
    auto exports = provider.createExports(spec);
    auto configuration = provider.createConfiguration(spec);
    BOOST_REQUIRE(exports);
    BOOST_REQUIRE(configuration);
    BOOST_CHECK(exports.value()->contract() == spec.triplet().contract());
    BOOST_CHECK(configuration.value()->triplet() == spec.triplet());
    auto emptyOptions = provider.createImportOptions(spec, srt::JsonObject());
    BOOST_REQUIRE(emptyOptions);
    BOOST_CHECK(emptyOptions.value()->contract() == spec.triplet().contract());
    auto invalidOptions = provider.createImportOptions(spec, srt::JsonObject{
                                                                 {"unknown", true}
    });
    BOOST_REQUIRE(!invalidOptions);
    BOOST_CHECK(invalidOptions.error().code() == srt::Error::InvalidFormat);
    auto validators = provider.createImportValidators();
    BOOST_REQUIRE(validators);
    BOOST_CHECK(!validators.value().empty());
    for (const auto &validator : validators.value())
        BOOST_REQUIRE(validator->validateImports(spec));
}
