#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/ManifestValues.h>

#include "LuaSandbox.h"

namespace fs = std::filesystem;
namespace S2PApi = wolf::Api::S2P::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::lua {

    namespace {

        constexpr char VARIANT[] = "lua";

        /// Names of the two interpreters of this variant in diagnostics. The variant name alone
        /// does not identify the contract under which a rejected declaration was read.
        constexpr char LUA_S2P[] = "lua S2P";
        constexpr char LUA_ONSET[] = "lua onset";

        /// Global function that each contract requires the script to define.
        constexpr char S2P_ENTRY[] = "s2p";
        constexpr char ONSET_ENTRY[] = "markonset";

        /// Shape of the S2P contract exports: the language pairs the module consumes, its
        /// phonemes, and openSet.
        constexpr ExportsShape S2P_EXPORTS{true, "phonemes", false, true};

        /// Shape of the Onset contract exports: the phonemes that the rules recognize, as a lower
        /// bound.
        constexpr ExportsShape ONSET_EXPORTS{false, "knownPhonemes", false, false};

        /// Holds the script text for the executives of one module.
        ///
        /// The configuration holds the text rather than a running interpreter, because an
        /// interpreter state cannot be shared. Each executive compiles its own interpreter from
        /// this text.
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

        /// Reads the script and checks that it defines the global function \a entry.
        ///
        /// The script is compiled here as well as in every executive, so that a script that does
        /// not compile or does not define \a entry fails the package rather than the first
        /// conversion that uses it.
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
            // Lua includes the chunk name in messages, so the name is text rather than a path.
            result->chunkName = stdc::path::to_utf8(path);

            auto sandbox = Sandbox::create(result->source, result->chunkName);
            if (!sandbox) {
                return sandbox.takeError();
            }
            if (!(*sandbox)->hasFunction(entry)) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string("the script defines no global function named ") +
                                      entry + ": " + result->chunkName);
            }
            return result;
        }

        class S2PExecutive
            : public ExecutiveBase<S2PApi::S2PExecutive, S2PApi::S2PStartInput, S2PApi::S2PResult> {
        public:
            S2PExecutive(srt::InferenceSpec &spec, std::unique_ptr<Sandbox> sandbox)
                : ExecutiveBase(spec), m_sandbox(std::move(sandbox)) {
            }

            ~S2PExecutive() {
                finish();
            }

            srt::Expected<void> initialize(const S2PApi::S2PInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is the path by which a package
            // unload stops a running conversion.

        protected:
            Batch runBatch(const S2PApi::S2PStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the base class
                // and this call is honored rather than cleared. A request left over from an
                // earlier conversion cancels a batch that the base class did not intend to cancel;
                // the base class detects this case and retries the batch.
                auto result = std::make_unique<S2PApi::S2PResult>();
                result->phonemes.reserve(input.pronunciations.size());
                for (const auto &pronunciation : input.pronunciations) {
                    if (stopRequested()) {
                        return result;
                    }
                    auto converted = m_sandbox->callForStrings(S2P_ENTRY, pronunciation);
                    if (!converted) {
                        // A script call ended by the interrupt is reported as cancelled rather
                        // than as failed: returning the completed part lets the task interface
                        // settle the task as Canceled, whereas an error would settle it as
                        // Failed.
                        if (stopRequested()) {
                            return result;
                        }
                        return converted.takeError();
                    }
                    result->phonemes.push_back(converted.take());
                }
                return result;
            }

            /// Interrupts the script call in progress as well, because a script can loop without
            /// reaching a word boundary.
            void onStop() override {
                m_sandbox->interrupt();
            }

            /// Clears the interrupt left by a stop, so that the next run can call the script.
            void onSettled() override {
                m_sandbox->resume();
            }

        private:
            std::unique_ptr<Sandbox> m_sandbox;
        };

        class OnsetExecutive
            : public ExecutiveBase<OnsetApi::OnsetExecutive, OnsetApi::OnsetStartInput,
                                   OnsetApi::OnsetResult> {
        public:
            OnsetExecutive(srt::InferenceSpec &spec, std::unique_ptr<Sandbox> sandbox)
                : ExecutiveBase(spec), m_sandbox(std::move(sandbox)) {
            }

            ~OnsetExecutive() {
                finish();
            }

            srt::Expected<void> initialize(const OnsetApi::OnsetInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is the path by which a package
            // unload stops a running conversion.

        protected:
            Batch runBatch(const OnsetApi::OnsetStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the base class
                // and this call is honored rather than cleared. A request left over from an
                // earlier conversion cancels a batch that the base class did not intend to cancel;
                // the base class detects this case and retries the batch.
                auto result = std::make_unique<OnsetApi::OnsetResult>();
                result->onsets.reserve(input.phonemes.size());
                for (const auto &word : input.phonemes) {
                    if (stopRequested()) {
                        return result;
                    }
                    auto marked = m_sandbox->callForFlags(ONSET_ENTRY, word);
                    if (!marked) {
                        if (stopRequested()) {
                            return result;
                        }
                        return marked.takeError();
                    }
                    result->onsets.push_back(marked.take());
                }
                return result;
            }

            /// Interrupts the script call in progress as well, because a script can loop without
            /// reaching a word boundary.
            void onStop() override {
                m_sandbox->interrupt();
            }

            /// Clears the interrupt left by a stop, so that the next run can call the script.
            void onSettled() override {
                m_sandbox->resume();
            }

        private:
            std::unique_ptr<Sandbox> m_sandbox;
        };

        class S2PInterpreter : public srt::InferenceInterpreter {
        public:
            using Configuration = S2PConfiguration;

            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto read = readContractExports(spec, S2P_EXPORTS, "exports");
                if (!read) {
                    return read.takeError();
                }
                auto result = std::make_unique<S2PApi::S2PExports>(VARIANT);
                result->languages = std::move(read->languages);
                result->phonemes = std::move(read->strings);
                result->openSet = read->openSet;
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
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, LUA_S2P);
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
                auto read = readContractExports(spec, ONSET_EXPORTS, "exports");
                if (!read) {
                    return read.takeError();
                }
                auto result = std::make_unique<OnsetApi::OnsetExports>(VARIANT);
                result->knownPhonemes = std::move(read->strings);
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
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, LUA_ONSET);
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
