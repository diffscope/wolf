#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Support/ExecutiveTask.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/ResourceCache.h>

#include "S2PTables.h"

namespace fs = std::filesystem;
namespace S2PApi = wolf::Api::S2P::L1;

namespace wolf::s2p {

    namespace {

        constexpr char DIRECT[] = "direct";
        constexpr char DICT[] = "dict";
        constexpr char MAPPING[] = "mapping";

        /// The parser generation of the tables, bumped when their syntax changes so cached entries
        /// from an older reading stop matching without anyone invalidating them.
        constexpr char DICT_KIND[] = "s2p-dict-tsv@1";
        constexpr char MAPPING_KIND[] = "s2p-mapping-tsv@1";

        /// Holds whichever table the variant uses. Tables come from the shared cache, so two
        /// executives over the same file parse it once.
        using Table = std::variant<std::monostate, std::shared_ptr<const DictionaryTable>,
                                   std::shared_ptr<const MappingTable>>;

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration(std::string variant)
                : ContribConfiguration(S2PApi::API_INTERFACE, std::move(variant),
                                       S2PApi::API_LEVEL) {
            }

            /// Absent for direct, which needs no resource at all.
            fs::path file;

            /// Parsed during Acquire rather than when an executive is created, because the upper
            /// specification has the provider validate its configuration then, and a malformed
            /// dictionary should fail the package rather than the first conversion that touches it.
            Table table;
        };

        /// Reads the one configuration key these variants have.
        ///
        /// Unknown keys are rejected rather than ignored, so a misspelling fails loudly instead of
        /// silently leaving a default in place.
        srt::Expected<fs::path> readFile(const srt::ContribSpec &spec, bool required) {
            const auto &value = spec.manifestConfiguration();
            if (!value.isObject()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the S2P configuration must be an object");
            }
            fs::path file;
            for (const auto &[key, item] : value.toObject()) {
                if (key != "file") {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown S2P configuration key: " + key);
                }
                if (!item.isString()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the S2P configuration file must be a string");
                }
                file = pathFromManifest(item.toString());
            }
            if (file.empty()) {
                if (required) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "this S2P variant requires a configuration file");
                }
                return file;
            }
            if (file.is_relative()) {
                file = spec.declarationPath().parent_path() / file;
            }
            return file.lexically_normal();
        }

        /// Loads the table a variant needs, sharing it with anything else reading the same file.
        srt::Expected<Table> loadTable(const std::string &variant, const fs::path &file) {
            auto &cache = ResourceCache::instance();
            if (variant == DICT) {
                auto loaded = cache.acquire<DictionaryTable>(
                    file, DICT_KIND, variant,
                    std::function([](const fs::path &path)
                                      -> srt::Expected<std::shared_ptr<const DictionaryTable>> {
                        auto parsed = DictionaryTable::load(path);
                        if (!parsed) {
                            return parsed.takeError();
                        }
                        return std::make_shared<const DictionaryTable>(parsed.take());
                    }));
                if (!loaded) {
                    return loaded.takeError();
                }
                return Table(loaded.take());
            }
            if (variant == MAPPING) {
                auto loaded = cache.acquire<MappingTable>(
                    file, MAPPING_KIND, variant,
                    std::function([](const fs::path &path)
                                      -> srt::Expected<std::shared_ptr<const MappingTable>> {
                        auto parsed = MappingTable::load(path);
                        if (!parsed) {
                            return parsed.takeError();
                        }
                        return std::make_shared<const MappingTable>(parsed.take());
                    }));
                if (!loaded) {
                    return loaded.takeError();
                }
                return Table(loaded.take());
            }
            return Table{};
        }

        class Executive : public S2PApi::S2PExecutive {
        public:
            Executive(srt::InferenceSpec &spec, Table table)
                : S2PExecutive(spec), m_table(std::move(table)),
                  m_task([this](const S2PApi::S2PStartInput &input) { return runBatch(input); },
                         [this] { return m_stopRequested.exchange(false); }) {
            }

            ~Executive() {
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
                    // Cooperative cancellation answers between units, so a caller that stops mid
                    // batch gets what was finished rather than an error.
                    if (m_stopRequested) {
                        return result;
                    }
                    result->phonemes.push_back(convert(pronunciation));
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
                return m_task.stop();
            }

            srt::Expected<void> waitForFinished() override {
                return m_task.waitForFinished();
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is how a Package unload reaches
            // a running conversion.

        private:
            std::vector<std::string> convert(const std::string &pronunciation) const {
                if (auto dictionary =
                        std::get_if<std::shared_ptr<const DictionaryTable>>(&m_table)) {
                    return (*dictionary)->convert(pronunciation);
                }
                if (auto mapping = std::get_if<std::shared_ptr<const MappingTable>>(&m_table)) {
                    return (*mapping)->convert(pronunciation);
                }
                return splitPronunciation(pronunciation);
            }

            Table m_table;
            std::atomic_bool m_stopRequested = false;

            /// Last, so it is the first member destroyed and its wait happens before the table.
            ExecutiveTask<S2PApi::S2PStartInput, S2PApi::S2PResult> m_task;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            explicit Interpreter(std::string variant) : m_variant(std::move(variant)) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto result = std::make_unique<S2PApi::S2PExports>(m_variant);
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
                auto file = readFile(spec, m_variant != DIRECT);
                if (!file) {
                    return file.takeError();
                }
                auto result = std::make_unique<Configuration>(m_variant);
                result->file = file.take();
                auto table = loadTable(m_variant, result->file);
                if (!table) {
                    return table.takeError();
                }
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
                    new S2PApi::S2PImportOptions(m_variant));
            }

            srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
                createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                const srt::InferenceRuntimeOptions &) override {
                auto configuration = static_cast<const Configuration *>(spec.configuration());
                return std::unique_ptr<srt::InferenceExecutive>(
                    new Executive(spec, configuration->table));
            }

        private:
            std::string m_variant;
        };

    }

    class S2PPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != S2PApi::API_INTERFACE || level != S2PApi::API_LEVEL) {
                return srt::Error(srt::Error::InvalidArgument, "unsupported S2P contract");
            }
            if (variant != DIRECT && variant != DICT && variant != MAPPING) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported S2P variant: " + std::string(variant));
            }
            return std::unique_ptr<srt::ContribInterpreter>(new Interpreter(std::string(variant)));
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::s2p::S2PPlugin)
