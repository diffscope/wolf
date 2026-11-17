#ifndef WOLF_TESTS_PLUGINS_SINGERPROVIDERS_STUBSINGER_STUBSINGERPROVIDER_H
#define WOLF_TESTS_PLUGINS_SINGERPROVIDERS_STUBSINGER_STUBSINGERPROVIDER_H

#include <memory>
#include <string>
#include <string_view>

#include <synthrt/SVS/SingerProvider.h>

namespace wolf::stub {

    /// The contract that the static registration in StubPlugins.cpp declares for this provider.
    ///
    /// The plugin metadata and this class have to agree on the triple: the loader matches a
    /// declaration against the metadata before it reaches this class, so a drift here would
    /// otherwise be invisible until something else compared the two.
    inline constexpr std::string_view INTERFACE = "org.openvpi.wolf.test.Singer";

    inline constexpr int LEVEL = 1;

    inline constexpr std::string_view VARIANT = "stub";

    /// Singer provider that accepts any configuration.
    ///
    /// The stub allows tests to exercise the linguist domain against a singer without a full
    /// DiffSinger voicebank. The configuration block is owned by the variant, and this variant
    /// imposes no constraints on its contents.
    ///
    /// SingerProvider already implements exports, import options and import bindings, so this class
    /// only accepts every configuration.
    class StubSingerProvider : public srt::SingerProvider {
    public:
        explicit StubSingerProvider(srt::ContribTriplet triplet);
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &) const override;

    private:
        class Configuration : public srt::ContribConfiguration {
        public:
            explicit Configuration(srt::ContribTriplet triplet)
                : ContribConfiguration(std::move(triplet)) {
            }
        };
        srt::ContribTriplet m_triplet;
    };

}

#endif // WOLF_TESTS_PLUGINS_SINGERPROVIDERS_STUBSINGER_STUBSINGERPROVIDER_H
