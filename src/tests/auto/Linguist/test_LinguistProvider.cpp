#include <filesystem>

#include <stdcorelib/plugin/pluginloader.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Linguist/LinguistProvider.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;
namespace LinguistApi = wolf::Api::Linguist::L1;

BOOST_AUTO_TEST_SUITE(test_LinguistProvider)

BOOST_AUTO_TEST_CASE(test_LinguistProvider_CreatesSupportedContract) {
    wolf::LinguistCategory category;
    BOOST_CHECK_EQUAL(category.name(), wolf::LINGUIST_CATEGORY);

    stdc::plugin::PluginLoader loader(WOLF_TEST_PLUGIN_FILE,
                                      fs::path(WOLF_TEST_PLUGIN_DIR) / "plugin.json");
    BOOST_REQUIRE_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.iid(), wolf::LinguistProviderPlugin::IID);

    auto plugin = static_cast<wolf::LinguistProviderPlugin *>(loader.plugin());
    BOOST_REQUIRE(plugin != nullptr);

    auto result = plugin->create(LinguistApi::API_INTERFACE, LinguistApi::API_LEVEL,
                                 LinguistApi::API_VARIANT);
    BOOST_REQUIRE(result);
    auto interpreter = result.take();
    BOOST_CHECK(interpreter->as<wolf::LinguistProvider>() != nullptr);

    auto validators = interpreter->createImportValidators();
    BOOST_REQUIRE(validators);
    BOOST_REQUIRE_EQUAL(validators->size(), 1u);
    BOOST_CHECK((*validators)[0] != nullptr);

    auto unsupported = plugin->create(LinguistApi::API_INTERFACE, LinguistApi::API_LEVEL + 1,
                                      LinguistApi::API_VARIANT);
    BOOST_CHECK(!unsupported);
}

BOOST_AUTO_TEST_SUITE_END()
