#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/ResourceCache.h>

#include "OnsetRules.h"

namespace fs = std::filesystem;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::onset {

    namespace {

        constexpr char RULE[] = "rule";

        /// Cache generation of the rule file parser. Raising it after a syntax change prevents old
        /// cache entries from matching without explicit invalidation.
        constexpr char RULE_KIND[] = "onset-rule-json@1";

        /// Exports of the Onset contract: the phonemes that the rules cover, as a lower bound.
        constexpr ExportsShape ONSET_EXPORTS{false, "knownPhonemes", false, false};

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(OnsetApi::API_INTERFACE, RULE, OnsetApi::API_LEVEL) {
            }

            std::shared_ptr<const RuleTable> table;
        };

        class Executive : public ExecutiveBase<OnsetApi::OnsetExecutive, OnsetApi::OnsetStartInput,
                                               OnsetApi::OnsetResult> {
        public:
            Executive(srt::InferenceSpec &spec, std::shared_ptr<const RuleTable> table)
                : ExecutiveBase(spec), m_table(std::move(table)) {
            }

            ~Executive() {
                finish();
            }

            srt::Expected<void> initialize(const OnsetApi::OnsetInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const OnsetApi::OnsetStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the parent
                // executive and this call is honored rather than cleared. A request left over from
                // an earlier conversion cancels a batch that the parent executive did not stop;
                // the parent executive detects this case and runs the batch again.
                auto result = std::make_unique<OnsetApi::OnsetResult>();
                result->onsets.reserve(input.phonemes.size());
                for (const auto &sequence : input.phonemes) {
                    // Cancellation is checked between words, so a caller that stops the batch
                    // receives the words completed so far rather than an error.
                    if (stopRequested()) {
                        return result;
                    }

                    result->onsets.push_back(m_table->mark(sequence));
                }
                return result;
            }

        private:
            std::shared_ptr<const RuleTable> m_table;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto read = readContractExports(spec, ONSET_EXPORTS, "exports");
                if (!read) {
                    return read.takeError();
                }
                auto result = std::make_unique<OnsetApi::OnsetExports>(RULE);
                result->knownPhonemes = std::move(read->strings);
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

                // The rule file is parsed during Acquire, so a malformed rule file fails the
                // package load rather than the first sequence that uses the rules.
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
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, RULE); !checked) {
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
