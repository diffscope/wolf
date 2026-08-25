#ifndef WOLF_API_LANGUAGEAPIL1_H
#define WOLF_API_LANGUAGEAPIL1_H

#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>

#include <wolf/Language/LanguagePipelineExecutive.h>
#include <wolf/wolf_global.h>

namespace wolf::Api::Language::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.language.WolfLanguage";
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

    /// Supplies runtime settings when a language executive is created.
    class LanguageRuntimeOptions : public srt::ContribRuntimeOptions {
    public:
        LanguageRuntimeOptions() : ContribRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Owns runtime activity associated with one language contribution.
    class WOLF_EXPORT LanguageExecutive : public wolf::LanguagePipelineExecutive {
    public:
        explicit LanguageExecutive(wolf::LanguageSpec &spec) : LanguagePipelineExecutive(spec) {
        }

        ~LanguageExecutive() = default;
    };

    /// Supplies runtime settings when the wolf language pipeline is created.
    class WolfPipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        WolfPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Aggregates the language contributions imported by one singer.
    class WOLF_EXPORT WolfPipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        ~WolfPipelineExecutive() = default;

        /// Returns the singer local roles of all aggregated language imports.
        virtual const std::vector<std::string> &languageRoles() const = 0;

        /// Creates the language executive selected by a role.
        virtual srt::Expected<LanguageExecutive *>
            createLanguage(std::string_view role, const LanguageRuntimeOptions &runtimeOptions) = 0;

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

    /// Creates a wolf language pipeline from the imports aggregated during Package Load.
    class WOLF_EXPORT WolfPipelineExtension : public srt::SingerPipelineExtension {
    public:
        ~WolfPipelineExtension() = default;

        /// Returns the singer local roles of all aggregated language imports.
        virtual const std::vector<std::string> &languageRoles() const = 0;

    protected:
        using SingerPipelineExtension::SingerPipelineExtension;
    };

}

namespace srt {

    template <>
    struct ContribSpecExtensionTraits<SingerSpec, wolf::Api::Language::L1::WolfPipelineExecutive> {
        inline static constexpr char ID[] = "org.openvpi.wolf.extension.LanguagePipeline";
    };

}

#endif // WOLF_API_LANGUAGEAPIL1_H
