#ifndef WOLF_TESTS_PLUGINS_LINGUISTPROVIDERS_WOLFLINGUIST_TESTSUPPORT_H
#define WOLF_TESTS_PLUGINS_LINGUISTPROVIDERS_WOLFLINGUIST_TESTSUPPORT_H

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>
#include <wolf/Linguist/SingerLanguages.h>

#include <wolflinguist/WolfLinguistProvider.h>

#include "../../../TestSupport.h"

namespace {
    class ProviderPlugin : public wolf::LinguistProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            namespace Api = wolf::Api::Linguist::L1;
            if (triplet !=
                srt::ContribContractTriplet(Api::API_INTERFACE, Api::API_LEVEL, Api::API_VARIANT))
                return srt::Error(srt::Error::InvalidArgument, "unknown linguist contract");
            return std::make_unique<wolf::WolfLinguistProvider>();
        }
    };
    STDC_EXPORT_STATIC_PLUGIN(ProviderPlugin, wolf::LinguistProviderPlugin::IID,
                              wolf::test::readPluginManifest(WOLF_TEST_PLUGIN_MANIFEST))

    /// Holds a minimal singer tree using the real provider and test inference stages.
    struct LoadedTree {
        srt::SynthUnit unit;
        srt::PackageHandle package;
        LoadedTree() {
            wolf::test::configure(unit, {wolf::test::fixtureRoot()});
            package = wolf::test::load(unit, wolf::test::fixtureRoot() / "singer-zxx");
        }
        srt::SingerSpec &singer() {
            auto spec = package.contribution("singer", "s");
            BOOST_REQUIRE(spec);
            return *spec->as<srt::SingerSpec>();
        }
        wolf::LinguistSpec &linguist() {
            auto imported = singer().findImport("lang/passthrough");
            BOOST_REQUIRE(imported);
            BOOST_REQUIRE(imported->binding());
            return *imported->binding()->target().as<wolf::LinguistSpec>();
        }
    };
}

#endif // WOLF_TESTS_PLUGINS_LINGUISTPROVIDERS_WOLFLINGUIST_TESTSUPPORT_H
