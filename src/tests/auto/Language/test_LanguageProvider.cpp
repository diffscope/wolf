#include <filesystem>

#include <stdcorelib/plugin/pluginloader.h>

#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageContrib.h>
#include <wolf/Language/LanguageProvider.h>
#include <wolf/Language/LanguageProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;
namespace Lang = wolf::Api::Language::L1;

BOOST_AUTO_TEST_SUITE(test_LanguageProvider)

BOOST_AUTO_TEST_CASE(test_LanguageProvider_CreatesSupportedContract) {
    wolf::LanguageCategory category;
    BOOST_CHECK_EQUAL(category.name(), wolf::LANGUAGE_CATEGORY);

    stdc::plugin::PluginLoader loader(WOLF_TEST_PLUGIN_FILE,
                                      fs::path(WOLF_TEST_PLUGIN_DIR) / "plugin.json");
    BOOST_REQUIRE_EQUAL(loader.state(), stdc::plugin::PluginLoader::Read);
    BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
    BOOST_CHECK_EQUAL(loader.iid(), wolf::LanguageProviderPlugin::IID);

    auto plugin = static_cast<wolf::LanguageProviderPlugin *>(loader.plugin());
    BOOST_REQUIRE(plugin != nullptr);

    auto result = plugin->create(Lang::API_INTERFACE, Lang::API_LEVEL, Lang::API_VARIANT);
    BOOST_REQUIRE(result);
    auto interpreter = result.take();
    BOOST_CHECK(interpreter->as<wolf::LanguageProvider>() != nullptr);

    auto validators = interpreter->createImportValidators();
    BOOST_REQUIRE(validators);
    BOOST_REQUIRE_EQUAL(validators->size(), 1u);
    BOOST_CHECK((*validators)[0] != nullptr);

    auto unsupported = plugin->create(Lang::API_INTERFACE, Lang::API_LEVEL + 1, Lang::API_VARIANT);
    BOOST_CHECK(!unsupported);
}

BOOST_AUTO_TEST_SUITE_END()
