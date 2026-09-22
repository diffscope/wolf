#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Support/ExecutiveTask.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/ResourceCache.h>

#include "OnsetRules.h"

namespace fs = std::filesystem;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::onset {

    namespace {

        constexpr char RULE[] = "rule";

        /// The parser generation of the rule file, so a syntax change stops matching old cache
        /// entries without anyone invalidating them.
        constexpr char RULE_KIND[] = "onset-rule-json@1";

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(OnsetApi::API_INTERFACE, RULE, OnsetApi::API_LEVEL) {
            }

            std::shared_ptr<const RuleTable> table;
        };

        class Executive : public OnsetApi::OnsetExecutive {
        public:
            Executive(srt::InferenceSpec &spec, std::shared_ptr<const RuleTable> table)
                : OnsetExecutive(spec), m_table(std::move(table)),
                  m_task([this](const OnsetApi::OnsetStartInput &input) { return runBatch(input); },
                         [this] { return m_stopRequested.exchange(false); }) {
            }

            ~Executive() {
                (void) m_task.waitForFinished();
            }

            srt::Expected<void> initialize(const OnsetApi::OnsetInitArgs &) override {
                return {};
            }

            srt::Expected<std::unique_ptr<OnsetApi::OnsetResult>>
                start(const OnsetApi::OnsetStartInput &input) override {
                return m_task.run(input);
            }

            srt::Expected<void> startAsync(std::shared_ptr<const OnsetApi::OnsetStartInput> input,
                                           AsyncCallback callback) override {
                return m_task.runAsync(std::move(input), std::move(callback));
            }

            srt::ITask::State state() const noexcept override {
                return m_task.state();
            }

            srt::Expected<void> stop() override {
                m_stopRequested = true;
                return m_task.stop();
            }

            srt::Expected<void> waitForFinished() override {
                return m_task.waitForFinished();
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
                for (const auto &sequence : input.phonemes) {
                    // Cooperative cancellation answers between words, so a caller that stops
                    // mid batch gets what was finished rather than an error.
                    if (m_stopRequested) {
                        return result;
                    }

                    result->onsets.push_back(m_table->mark(sequence));
                }
                return result;
            }

            std::shared_ptr<const RuleTable> m_table;
            std::atomic_bool m_stopRequested = false;

            /// Last, so it is the first member destroyed and its wait runs before the
            /// fields the body reads are gone.
            ExecutiveTask<OnsetApi::OnsetStartInput, OnsetApi::OnsetResult> m_task;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto result = std::make_unique<OnsetApi::OnsetExports>(RULE);
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
                        auto known = readStringSet(it->second, spec.declarationPath().parent_path(),
                                                   "exports knownPhonemes");
                        if (!known) {
                            return known.takeError();
                        }
                        result->knownPhonemes = known.take();
                    }
                }
                return std::unique_ptr<srt::ContribExports>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
                createConfiguration(const srt::ContribSpec &spec) const override {
                const auto &value = spec.manifestConfiguration();
                if (!value.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the onset configuration must be an object");
                }
                fs::path file;
                for (const auto &[key, item] : value.toObject()) {
                    if (key != "file") {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "unknown onset configuration key: " + key);
                    }
                    if (!item.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "the onset configuration file must be a string");
                    }
                    file = pathFromManifest(item.toString());
                }
                if (file.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the rule variant requires a configuration file");
                }
                if (file.is_relative()) {
                    file = spec.declarationPath().parent_path() / file;
                }

                // Parsed during Acquire, so a malformed rule file fails the package rather than
                // the first sequence that reaches it.
                auto table = ResourceCache::instance().acquire<RuleTable>(
                    file.lexically_normal(), RULE_KIND, RULE,
                    std::function([](const fs::path &path)
                                      -> srt::Expected<std::shared_ptr<const RuleTable>> {
                        auto parsed = RuleTable::load(path);
                        if (!parsed) {
                            return parsed.takeError();
                        }
                        return std::make_shared<const RuleTable>(parsed.take());
                    }));
                if (!table) {
                    return table.takeError();
                }
                auto result = std::make_unique<Configuration>();
                result->table = table.take();
                return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
                createImportOptions(const srt::ContribSpec &,
                                    const srt::JsonValue &manifestOptions) const override {
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, "inference");
                    !checked) {
                    return checked.takeError();
                }
                return std::unique_ptr<srt::ContribImportOptions>(
                    new OnsetApi::OnsetImportOptions(RULE));
            }

            srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
                createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                const srt::InferenceRuntimeOptions &) override {
                auto configuration = static_cast<const Configuration *>(spec.configuration());
                return std::unique_ptr<srt::InferenceExecutive>(
                    new Executive(spec, configuration->table));
            }
        };

    }

    class OnsetPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != OnsetApi::API_INTERFACE || level != OnsetApi::API_LEVEL ||
                variant != RULE) {
                return srt::Error(srt::Error::InvalidArgument, "unsupported onset contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new Interpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::onset::OnsetPlugin)
