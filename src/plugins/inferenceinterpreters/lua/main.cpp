#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Support/ExecutiveTask.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Support/ManifestValues.h>

#include "LuaSandbox.h"

namespace fs = std::filesystem;
namespace S2PApi = wolf::Api::S2P::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::lua {

    namespace {

        constexpr char VARIANT[] = "lua";

        /// The global each contract expects the script to define.
        constexpr char S2P_ENTRY[] = "s2p";
        constexpr char ONSET_ENTRY[] = "markonset";

        /// Holds the script text for the executives of one module.
        ///
        /// The text rather than a running interpreter: an interpreter state cannot be shared, so
        /// each executive compiles its own from this.
        class S2PConfiguration : public srt::ContribConfiguration {
        public:
            S2PConfiguration()
                : ContribConfiguration(S2PApi::API_INTERFACE, VARIANT, S2PApi::API_LEVEL) {
            }

            std::string source;
            std::string chunkName;
        };

        class OnsetConfiguration : public srt::ContribConfiguration {
        public:
            OnsetConfiguration()
                : ContribConfiguration(OnsetApi::API_INTERFACE, VARIANT, OnsetApi::API_LEVEL) {
            }

            std::string source;
            std::string chunkName;
        };

        srt::Expected<fs::path> readFileKey(const srt::ContribSpec &spec) {
            const auto &value = spec.manifestConfiguration();
            if (!value.isObject()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the lua configuration must be an object");
            }
            fs::path file;
            for (const auto &[key, item] : value.toObject()) {
                if (key != "file") {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown lua configuration key: " + key);
                }
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the lua configuration file must be a non-empty string");
                }
                file = pathFromManifest(item.toString());
            }
            if (file.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "the lua configuration needs a file");
            }
            if (file.is_relative()) {
                file = spec.declarationPath().parent_path() / file;
            }
            return file.lexically_normal();
        }

        /// Reads the script and checks that it defines what the contract asks for.
        ///
        /// Compiling it here as well as in every executive is deliberate: a script that does not
        /// compile, or that defines nothing callable, should fail the package rather than the
        /// first conversion that reaches it.
        template <class Configuration>
        srt::Expected<std::unique_ptr<Configuration>>
            readScriptConfiguration(const srt::ContribSpec &spec, const char *entry) {
            auto file = readFileKey(spec);
            if (!file) {
                return file.takeError();
            }
            const auto path = file.take();
            auto source = readScript(path);
            if (!source) {
                return source.takeError();
            }
            auto result = std::make_unique<Configuration>();
            result->source = source.take();
            // Lua names the chunk in messages, so the name is text rather than a path.
            result->chunkName = stdc::path::to_utf8(path);

            auto sandbox = Sandbox::create(result->source, result->chunkName);
            if (!sandbox) {
                return sandbox.takeError();
            }
            if (!(*sandbox)->hasFunction(entry)) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string("the script defines no global function called ") +
                                      entry + ": " + result->chunkName);
            }
            return result;
        }

        class S2PExecutive : public S2PApi::S2PExecutive {
        public:
            S2PExecutive(srt::InferenceSpec &spec, std::unique_ptr<Sandbox> sandbox)
                : S2PApi::S2PExecutive(spec), m_sandbox(std::move(sandbox)),
                  m_task([this](const S2PApi::S2PStartInput &input) { return runBatch(input); },
                         [this] {
                             m_sandbox->resume();
                             return m_stopRequested.exchange(false);
                         }) {
            }

            ~S2PExecutive() {
                (void) m_task.waitForFinished();
            }

            srt::Expected<void> initialize(const S2PApi::S2PInitArgs &) override {
                return {};
            }

            srt::Expected<std::unique_ptr<S2PApi::S2PResult>>
                start(const S2PApi::S2PStartInput &input) override {
                return m_task.run(input);
            }

        private:
            srt::Expected<std::unique_ptr<S2PApi::S2PResult>>
                runBatch(const S2PApi::S2PStartInput &input) {
                // A request pending on entry cancels this batch. A stop is consumed only after a
                // run, so one that landed between the parent's check and this call is honored
                // rather than cleared away; one left over from an earlier conversion cancels a
                // batch the parent never meant to cancel, which the parent recognizes and retries.
                auto result = std::make_unique<S2PApi::S2PResult>();
                result->phonemes.reserve(input.pronunciations.size());
                for (const auto &pronunciation : input.pronunciations) {
                    if (m_stopRequested) {
                        return result;
                    }
                    auto converted = m_sandbox->callForStrings(S2P_ENTRY, pronunciation);
                    if (!converted) {
                        // A script the interrupt cut short reports as cancelled, not as broken:
                        // returning what it finished lets the task face settle it as Canceled,
                        // where an error would settle it as Failed.
                        if (m_stopRequested) {
                            return result;
                        }
                        return converted.takeError();
                    }
                    result->phonemes.push_back(converted.take());
                }
                return result;
            }

        public:
            srt::Expected<void> startAsync(std::shared_ptr<const S2PApi::S2PStartInput> input,
                                           AsyncCallback callback) override {
                return m_task.runAsync(std::move(input), std::move(callback));
            }

            srt::ITask::State state() const noexcept override {
                return m_task.state();
            }

            srt::Expected<void> stop() override {
                m_stopRequested = true;
                // Also reaches inside a call: a script can loop without ever reaching a boundary.
                m_sandbox->interrupt();
                return m_task.stop();
            }

            srt::Expected<void> waitForFinished() override {
                return m_task.waitForFinished();
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is how a Package unload reaches
            // a running conversion.

        private:
            std::unique_ptr<Sandbox> m_sandbox;
            std::atomic_bool m_stopRequested = false;

            /// Last, so it is the first member destroyed and its wait runs before the sandbox.
            ExecutiveTask<S2PApi::S2PStartInput, S2PApi::S2PResult> m_task;
        };

        class OnsetExecutive : public OnsetApi::OnsetExecutive {
        public:
            OnsetExecutive(srt::InferenceSpec &spec, std::unique_ptr<Sandbox> sandbox)
                : OnsetApi::OnsetExecutive(spec), m_sandbox(std::move(sandbox)),
                  m_task([this](const OnsetApi::OnsetStartInput &input) { return runBatch(input); },
                         [this] {
                             m_sandbox->resume();
                             return m_stopRequested.exchange(false);
                         }) {
            }

            ~OnsetExecutive() {
                (void) m_task.waitForFinished();
            }

            srt::Expected<void> initialize(const OnsetApi::OnsetInitArgs &) override {
                return {};
            }

            srt::Expected<std::unique_ptr<OnsetApi::OnsetResult>>
                start(const OnsetApi::OnsetStartInput &input) override {
                return m_task.run(input);
            }

        private:
            srt::Expected<std::unique_ptr<OnsetApi::OnsetResult>>
                runBatch(const OnsetApi::OnsetStartInput &input) {
                // A request pending on entry cancels this batch. A stop is consumed only after a
                // run, so one that landed between the parent's check and this call is honored
                // rather than cleared away; one left over from an earlier conversion cancels a
                // batch the parent never meant to cancel, which the parent recognizes and retries.
                auto result = std::make_unique<OnsetApi::OnsetResult>();
                result->onsets.reserve(input.phonemes.size());
                for (const auto &word : input.phonemes) {
                    if (m_stopRequested) {
                        return result;
                    }
                    auto marked = m_sandbox->callForFlags(ONSET_ENTRY, word);
                    if (!marked) {
                        if (m_stopRequested) {
                            return result;
                        }
                        return marked.takeError();
                    }
                    result->onsets.push_back(marked.take());
                }
                return result;
            }

        public:
            srt::Expected<void> startAsync(std::shared_ptr<const OnsetApi::OnsetStartInput> input,
                                           AsyncCallback callback) override {
                return m_task.runAsync(std::move(input), std::move(callback));
            }

            srt::ITask::State state() const noexcept override {
                return m_task.state();
            }

            srt::Expected<void> stop() override {
                m_stopRequested = true;
                // Also reaches inside a call: a script can loop without ever reaching a boundary.
                m_sandbox->interrupt();
                return m_task.stop();
            }

            srt::Expected<void> waitForFinished() override {
                return m_task.waitForFinished();
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is how a Package unload reaches
            // a running conversion.

        private:
            std::unique_ptr<Sandbox> m_sandbox;
            std::atomic_bool m_stopRequested = false;

            /// Last, so it is the first member destroyed and its wait runs before the sandbox.
            ExecutiveTask<OnsetApi::OnsetStartInput, OnsetApi::OnsetResult> m_task;
        };

        class S2PInterpreter : public srt::InferenceInterpreter {
        public:
            using Configuration = S2PConfiguration;

            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto result = std::make_unique<S2PApi::S2PExports>(VARIANT);
                const auto &declared = spec.manifestExports();
                if (!declared.isNull() && !declared.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat, "exports must be an object");
                }
                if (declared.isObject()) {
                    const auto &object = declared.toObject();
                    if (auto checked = wolf::rejectUnknownKeys(
                            object, {"languages", "phonemes", "openSet"}, "exports");
                        !checked) {
                        return checked.takeError();
                    }
                    if (const auto it = object.find("languages"); it != object.end()) {
                        auto languages = readLanguageSchemes(it->second, "exports languages");
                        if (!languages) {
                            return languages.takeError();
                        }
                        result->languages = languages.take();
                    }
                    if (const auto it = object.find("phonemes"); it != object.end()) {
                        auto phonemes = readStringSet(
                            it->second, spec.declarationPath().parent_path(), "exports phonemes");
                        if (!phonemes) {
                            return phonemes.takeError();
                        }
                        result->phonemes = phonemes.take();
                    }
                    auto openSet = readFlag(object, "openSet", false, "exports");
                    if (!openSet) {
                        return openSet.takeError();
                    }
                    result->openSet = openSet.take();
                }
                return std::unique_ptr<srt::ContribExports>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
                createConfiguration(const srt::ContribSpec &spec) const override {
                auto result = readScriptConfiguration<Configuration>(spec, S2P_ENTRY);
                if (!result) {
                    return result.takeError();
                }
                return std::unique_ptr<srt::ContribConfiguration>(result.take());
            }

            srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
                createImportOptions(const srt::ContribSpec &,
                                    const srt::JsonValue &manifestOptions) const override {
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, "inference");
                    !checked) {
                    return checked.takeError();
                }
                return std::unique_ptr<srt::ContribImportOptions>(
                    new S2PApi::S2PImportOptions(VARIANT));
            }

            srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
                createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                const srt::InferenceRuntimeOptions &) override {
                const auto *configuration =
                    static_cast<const Configuration *>(spec.configuration());
                auto sandbox = Sandbox::create(configuration->source, configuration->chunkName);
                if (!sandbox) {
                    return sandbox.takeError();
                }
                return std::unique_ptr<srt::InferenceExecutive>(
                    new S2PExecutive(spec, sandbox.take()));
            }
        };

        class OnsetInterpreter : public srt::InferenceInterpreter {
        public:
            using Configuration = OnsetConfiguration;

            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto result = std::make_unique<OnsetApi::OnsetExports>(VARIANT);
                const auto &declared = spec.manifestExports();
                if (!declared.isNull() && !declared.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat, "exports must be an object");
                }
                if (declared.isObject()) {
                    const auto &object = declared.toObject();
                    if (auto checked =
                            wolf::rejectUnknownKeys(object, {"knownPhonemes"}, "exports");
                        !checked) {
                        return checked.takeError();
                    }
                    if (const auto it = object.find("knownPhonemes"); it != object.end()) {
                        auto phonemes =
                            readStringSet(it->second, spec.declarationPath().parent_path(),
                                          "exports knownPhonemes");
                        if (!phonemes) {
                            return phonemes.takeError();
                        }
                        result->knownPhonemes = phonemes.take();
                    }
                }
                return std::unique_ptr<srt::ContribExports>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
                createConfiguration(const srt::ContribSpec &spec) const override {
                auto result = readScriptConfiguration<Configuration>(spec, ONSET_ENTRY);
                if (!result) {
                    return result.takeError();
                }
                return std::unique_ptr<srt::ContribConfiguration>(result.take());
            }

            srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
                createImportOptions(const srt::ContribSpec &,
                                    const srt::JsonValue &manifestOptions) const override {
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, "inference");
                    !checked) {
                    return checked.takeError();
                }
                return std::unique_ptr<srt::ContribImportOptions>(
                    new OnsetApi::OnsetImportOptions(VARIANT));
            }

            srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
                createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                const srt::InferenceRuntimeOptions &) override {
                const auto *configuration =
                    static_cast<const Configuration *>(spec.configuration());
                auto sandbox = Sandbox::create(configuration->source, configuration->chunkName);
                if (!sandbox) {
                    return sandbox.takeError();
                }
                return std::unique_ptr<srt::InferenceExecutive>(
                    new OnsetExecutive(spec, sandbox.take()));
            }
        };

    }

    class LuaPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (variant != VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported variant: " + std::string(variant));
            }
            if (interfaceName == S2PApi::API_INTERFACE && level == S2PApi::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(new S2PInterpreter());
            }
            if (interfaceName == OnsetApi::API_INTERFACE && level == OnsetApi::API_LEVEL) {
                return std::unique_ptr<srt::ContribInterpreter>(new OnsetInterpreter());
            }
            return srt::Error(srt::Error::InvalidArgument, "unsupported scripted contract");
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::lua::LuaPlugin)
