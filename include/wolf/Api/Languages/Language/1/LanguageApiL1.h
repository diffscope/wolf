#ifndef WOLF_API_LANGUAGEAPIL1_H
#define WOLF_API_LANGUAGEAPIL1_H

#include <string>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/ContribExecInstance.h>

#include <wolf/wolf_global.h>

namespace wolf::Api::Language::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.language.Language";
    inline constexpr char API_VARIANT[] = "wolf";
    inline constexpr int API_LEVEL = 1;

    /// The phoneme inventory declared by one language composition.
    class LanguageExports : public srt::ContribExports {
    public:
        LanguageExports() : ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        std::vector<std::string> phonemes;
    };

    /// The wolf variant configuration for a language composition.
    class LanguageConfiguration : public srt::ContribConfiguration {
    public:
        LanguageConfiguration() : ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Options supplied when another contribution imports a language composition.
    class LanguageImportOptions : public srt::ContribImportOptions {
    public:
        LanguageImportOptions() : ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Supplies runtime settings when a language execution instance is created.
    class LanguageRuntimeOptions : public srt::ContribRuntimeOptions {
    public:
        LanguageRuntimeOptions() : ContribRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Owns runtime activity associated with one language contribution.
    class WOLF_EXPORT LanguageExecInstance : public srt::ContribExecInstance {
    public:
        explicit LanguageExecInstance(srt::ContribSpec &spec);
        ~LanguageExecInstance();

    protected:
        srt::Expected<void> quit() override;
        srt::Expected<void> wait() override;
    };

}

#endif // WOLF_API_LANGUAGEAPIL1_H
