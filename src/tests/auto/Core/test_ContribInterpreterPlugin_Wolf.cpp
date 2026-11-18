#include <filesystem>

#include <stdcorelib/plugin/pluginloader.h>

#include <synthrt/Core/ContribInterpreterPlugin.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    void checkPlugin(const std::filesystem::path &file, const std::filesystem::path &metadata,
                     const char *iid) {
        stdc::plugin::PluginLoader loader(file, metadata);
        BOOST_REQUIRE(loader.state() == stdc::plugin::PluginLoader::Read);
        auto declarations =
            srt::ContribInterpreterPlugin::readInterpreterDeclarations(loader.metadata());
        BOOST_REQUIRE(declarations);
        BOOST_CHECK(!declarations.value().empty());
        BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
        BOOST_CHECK_EQUAL(loader.iid(), iid);
        auto plugin = static_cast<srt::ContribInterpreterPlugin *>(loader.plugin());
        BOOST_REQUIRE(plugin);
        for (const auto &triplet : declarations.value()) {
            auto created = plugin->create(triplet);
            BOOST_REQUIRE(created);
            BOOST_REQUIRE(created.value());
            auto otherLevel =
                plugin->create({triplet.interfaceId(), triplet.level() + 1, triplet.variant()});
            BOOST_REQUIRE(!otherLevel);
            auto otherVariant =
                plugin->create({triplet.interfaceId(), triplet.level(), "com.example.unsupported"});
            BOOST_REQUIRE(!otherVariant);
            auto otherInterface =
                plugin->create({"com.example.Unsupported", triplet.level(), triplet.variant()});
            BOOST_REQUIRE(!otherInterface);
        }
    }

}


#ifdef WOLF_TEST_WOLFS2P_FILE
BOOST_AUTO_TEST_CASE(wolfs2pFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFS2P_FILE, WOLF_TEST_WOLFS2P_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFONSET_FILE
BOOST_AUTO_TEST_CASE(wolfonsetFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFONSET_FILE, WOLF_TEST_WOLFONSET_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFPINYIN_FILE
BOOST_AUTO_TEST_CASE(wolfpinyinFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFPINYIN_FILE, WOLF_TEST_WOLFPINYIN_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFCHAIN_FILE
BOOST_AUTO_TEST_CASE(wolfchainFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFCHAIN_FILE, WOLF_TEST_WOLFCHAIN_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFLUA_FILE
BOOST_AUTO_TEST_CASE(wolfluaFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFLUA_FILE, WOLF_TEST_WOLFLUA_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFMULTIG2P_FILE
BOOST_AUTO_TEST_CASE(wolfmultig2pFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFMULTIG2P_FILE, WOLF_TEST_WOLFMULTIG2P_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}
#endif

#ifdef WOLF_TEST_WOLFLINGUISTPROVIDER_FILE
BOOST_AUTO_TEST_CASE(wolflinguistproviderFactoryMatchesItsManifest) {
    checkPlugin(WOLF_TEST_WOLFLINGUISTPROVIDER_FILE, WOLF_TEST_WOLFLINGUISTPROVIDER_MANIFEST,
                wolf::LinguistProviderPlugin::IID);
}
#endif
