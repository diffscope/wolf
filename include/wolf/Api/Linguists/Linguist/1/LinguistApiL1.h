#ifndef WOLF_API_LINGUISTAPIL1_H
#define WOLF_API_LINGUISTAPIL1_H

#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>

#include <wolf/Linguist/LinguistPipelineExecutive.h>
#include <wolf/wolf_global.h>

namespace wolf::Api::Linguist::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.wolf.linguist.WolfLinguist";
    inline constexpr char API_VARIANT[] = "wolf";
    inline constexpr int API_LEVEL = 1;

    /// The phoneme inventory declared by one linguist composition.
    class LinguistExports : public srt::ContribExports {
    public:
        LinguistExports() : ContribExports(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        std::vector<std::string> phonemes;
    };

    /// The wolf variant configuration for a linguist composition.
    class LinguistConfiguration : public srt::ContribConfiguration {
    public:
        LinguistConfiguration() : ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Options supplied when another contribution imports a linguist composition.
    class LinguistImportOptions : public srt::ContribImportOptions {
    public:
        LinguistImportOptions() : ContribImportOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Supplies runtime settings when a linguist executive is created.
    class LinguistRuntimeOptions : public srt::ContribRuntimeOptions {
    public:
        LinguistRuntimeOptions() : ContribRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Owns runtime activity associated with one linguist contribution.
    class LinguistExecutive : public wolf::LinguistPipelineExecutive {
    public:
        explicit LinguistExecutive(wolf::LinguistSpec &spec) : LinguistPipelineExecutive(spec) {
        }

        ~LinguistExecutive() = default;
    };

    /// Supplies runtime settings when the wolf linguist pipeline is created.
    class WolfPipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        WolfPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Aggregates the linguist contributions imported by one singer.
    class WolfPipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        ~WolfPipelineExecutive() = default;

        /// Returns the singer local roles of all aggregated linguist imports.
        virtual const std::vector<std::string> &linguistRoles() const = 0;

        /// Creates the linguist executive selected by a role.
        virtual srt::Expected<LinguistExecutive *>
            createLinguist(std::string_view role, const LinguistRuntimeOptions &runtimeOptions) = 0;

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

    /// Creates a wolf linguist pipeline from the imports aggregated during Package Load.
    class WolfPipelineExtension : public srt::SingerPipelineExtension {
    public:
        ~WolfPipelineExtension() = default;

        /// Returns the singer local roles of all aggregated linguist imports.
        virtual const std::vector<std::string> &linguistRoles() const = 0;

    protected:
        using SingerPipelineExtension::SingerPipelineExtension;
    };

}

namespace srt {

    template <>
    struct ContribSpecExtensionTraits<SingerSpec, wolf::Api::Linguist::L1::WolfPipelineExecutive> {
        inline static constexpr char ID[] = "org.openvpi.wolf.extension.LinguistPipeline";
    };

}

#endif // WOLF_API_LINGUISTAPIL1_H
