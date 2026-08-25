#ifndef WOLF_API_WOLFPIPELINEAPIL1_H
#define WOLF_API_WOLFPIPELINEAPIL1_H

#include <string>
#include <string_view>
#include <vector>

#include <synthrt/SVS/SingerPipelineExecInstance.h>

#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

namespace wolf::Api::WolfPipeline::L1 {

    /// Identifies the wolf language pipeline contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.pipeline.Language";

    /// Identifies the standard wolf language pipeline variant.
    inline constexpr char API_VARIANT[] = "wolf";

    /// Identifies Level 1 of the wolf language pipeline contract.
    inline constexpr int API_LEVEL = 1;

    /// Identifies the extension attached to compatible singer declarations.
    inline constexpr char EXTENSION_ID[] = "org.openvpi.wolf.pipeline.Language";

    /// Supplies runtime settings when the wolf language pipeline is created.
    class WolfPipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        WolfPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Aggregates the language contributions imported by one singer.
    class WOLF_EXPORT WolfPipelineExecInstance : public srt::SingerPipelineExecInstance {
    public:
        ~WolfPipelineExecInstance();

        /// Returns the singer local roles of all aggregated language imports.
        virtual const std::vector<std::string> &languageRoles() const = 0;

        /// Creates the language execution instance selected by a role.
        virtual srt::Expected<Language::L1::LanguageExecInstance *>
            createLanguage(std::string_view role,
                           const Language::L1::LanguageRuntimeOptions &runtimeOptions) = 0;

    protected:
        using SingerPipelineExecInstance::SingerPipelineExecInstance;
    };

    /// Creates a wolf language pipeline from the imports aggregated during Package Load.
    class WOLF_EXPORT WolfPipelineExtension : public srt::SingerPipelineExtension {
    public:
        ~WolfPipelineExtension();

        /// Returns the singer local roles of all aggregated language imports.
        virtual const std::vector<std::string> &languageRoles() const = 0;

    protected:
        using SingerPipelineExtension::SingerPipelineExtension;
    };

}

#endif // WOLF_API_WOLFPIPELINEAPIL1_H
