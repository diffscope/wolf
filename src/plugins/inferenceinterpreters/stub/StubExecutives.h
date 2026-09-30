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

    /// Interprets the G2P contract with a module that copies every non-empty word unchanged and
    /// marks every empty word Skip.
    ///
    /// The stub allows packages to be loaded and the executive tree to be traversed before the
    /// real variants are ported. The stub declares the real contract triple, so no package requires
    /// a test-only form of its declaration. For wolf/lang-zxx the stub behavior is exact rather
    /// than simplified: a passthrough language is a G2P module that marks every word Copy.
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

    /// Interprets the Onset contract by marking no position as an onset, which is the default
    /// that the contract defines.
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
