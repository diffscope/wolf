#ifndef WOLF_STUBEXECUTIVES_H
#define WOLF_STUBEXECUTIVES_H

#include <memory>
#include <string>
#include <utility>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>

namespace wolf::stub {

    /// Interprets the G2P contract with a module that copies every word through.
    ///
    /// It exists so packages can be loaded and the executive tree walked before the real variants
    /// are ported. It declares the real triple, so no package needs a test-only spelling of its
    /// declaration. For wolf/lang-zxx the behaviour is not even a simplification: a passthrough
    /// language is exactly a G2P that marks every word Copy.
    class StubG2PInterpreter : public srt::InferenceInterpreter {
    public:
        explicit StubG2PInterpreter(std::string variant) : m_variant(std::move(variant)) {
        }

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;

    private:
        std::string m_variant;
    };

    /// Interprets the S2P contract by splitting a pronunciation on spaces.
    class StubS2PInterpreter : public srt::InferenceInterpreter {
    public:
        explicit StubS2PInterpreter(std::string variant) : m_variant(std::move(variant)) {
        }

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;

    private:
        std::string m_variant;
    };

    /// Interprets the Onset contract by marking nothing, which is the contract's own default.
    class StubOnsetInterpreter : public srt::InferenceInterpreter {
    public:
        explicit StubOnsetInterpreter(std::string variant) : m_variant(std::move(variant)) {
        }

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;
        srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
            createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &importOptions,
                            const srt::InferenceRuntimeOptions &runtimeOptions) override;

    private:
        std::string m_variant;
    };

}

#endif // WOLF_STUBEXECUTIVES_H
