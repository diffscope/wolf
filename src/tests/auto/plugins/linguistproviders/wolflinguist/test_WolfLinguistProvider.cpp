#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(provider_parses_typed_contract_and_rejects_nonempty_import_options) {
    LoadedTree tree;
    wolf::WolfLinguistProvider provider;
    auto &spec = tree.linguist();
    auto exports = provider.createExports(spec);
    auto configuration = provider.createConfiguration(spec);
    BOOST_REQUIRE(exports);
    BOOST_REQUIRE(configuration);
    BOOST_CHECK(exports.value()->triplet() == spec.triplet());
    BOOST_CHECK(configuration.value()->triplet() == spec.triplet());
    auto emptyOptions = provider.createImportOptions(spec, srt::JsonObject());
    BOOST_REQUIRE(emptyOptions);
    BOOST_CHECK(emptyOptions.value()->triplet() == spec.triplet());
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
