#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <synthrt/SVS/SingerProvider.h>
#include <synthrt/SVS/SingerProviderPlugin.h>

namespace wolf::stub {

    /// The contract that `plugin.json` declares for this plugin.
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
        explicit StubSingerProvider(srt::ContribTriplet triplet) : m_triplet(std::move(triplet)) {
        }

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &) const override {
            return std::unique_ptr<srt::ContribConfiguration>(new Configuration(m_triplet));
        }

    private:
        class Configuration : public srt::ContribConfiguration {
        public:
            explicit Configuration(srt::ContribTriplet triplet)
                : ContribConfiguration(std::move(triplet)) {
            }
        };

        srt::ContribTriplet m_triplet;
    };

    class StubSingerPlugin final : public srt::SingerProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            if (triplet !=
                srt::ContribTriplet(std::string(INTERFACE), LEVEL, std::string(VARIANT))) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported stub singer contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new StubSingerProvider(triplet));
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::stub::StubSingerPlugin)
