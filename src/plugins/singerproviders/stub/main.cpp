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
        explicit StubSingerProvider(std::string interfaceName, std::string variant, int level)
            : m_interface(std::move(interfaceName)), m_variant(std::move(variant)), m_level(level) {
        }

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &) const override {
            return std::unique_ptr<srt::ContribConfiguration>(
                new Configuration(m_interface, m_variant, m_level));
        }

    private:
        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration(std::string interfaceName, std::string variant, int level)
                : ContribConfiguration(std::move(interfaceName), std::move(variant), level) {
            }
        };

        std::string m_interface;
        std::string m_variant;
        int m_level;
    };

    class StubSingerPlugin final : public srt::SingerProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != INTERFACE || level != LEVEL || variant != VARIANT) {
                return srt::Error(srt::Error::InvalidArgument, "unsupported stub singer contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(
                new StubSingerProvider(std::string(interfaceName), std::string(variant), level));
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::stub::StubSingerPlugin)
