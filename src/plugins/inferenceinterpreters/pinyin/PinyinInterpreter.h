#ifndef WOLF_INFERENCEINTERPRETERS_PINYIN_PINYININTERPRETER_H
#define WOLF_INFERENCEINTERPRETERS_PINYIN_PINYININTERPRETER_H

#include <memory>

#include <synthrt/SVS/InferenceInterpreter.h>

namespace wolf::pinyin {

    inline constexpr char VARIANT[] = "algo-pinyin";

    /// Interprets G2P modules of the algo-pinyin variant, which convert through cpp-pinyin.
    class PinyinInterpreter : public srt::InferenceInterpreter {
    public:
        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;
    };

}

#endif // WOLF_INFERENCEINTERPRETERS_PINYIN_PINYININTERPRETER_H
