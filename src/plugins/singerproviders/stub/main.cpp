#include <memory>
#include <string>
#include <utility>

#include <synthrt/SVS/SingerProvider.h>
#include <synthrt/SVS/SingerProviderPlugin.h>

namespace wolf::stub {

    /// A singer provider that accepts any configuration.
    ///
    /// It exists so tests can exercise the linguist domain against a singer without dragging in a
    /// full DiffSinger voicebank. It is also what lets the language map ride in the singer
    /// configuration until synthrt carries the declaration fields: the configuration block is
    /// owned by the variant, and this variant claims nothing about its contents.
    ///
    /// SingerProvider already implements exports, import options and import bindings, so the only
    /// thing left to say is that any configuration is acceptable.
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
            return std::unique_ptr<srt::ContribInterpreter>(
                new StubSingerProvider(std::string(interfaceName), std::string(variant), level));
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::stub::StubSingerPlugin)
