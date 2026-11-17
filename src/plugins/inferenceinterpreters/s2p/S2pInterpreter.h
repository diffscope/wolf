#ifndef WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H

#include <memory>
#include <string>

#include <synthrt/SVS/InferenceInterpreter.h>

namespace wolf::s2p {

    inline constexpr char DIRECT[] = "direct";

    inline constexpr char DICT[] = "dict";

    inline constexpr char MAPPING[] = "mapping";

    /// Interprets S2P modules of the direct, dict and mapping variants. The variant is
    /// fixed when the interpreter is created.
    class S2pInterpreter : public srt::InferenceInterpreter {
    public:
        explicit S2pInterpreter(std::string variant);
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &) override;

    private:
        std::string m_variant;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_S2P_S2PINTERPRETER_H
