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
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
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

        /// Exports of the S2P contract: the language/scheme pairs that the module consumes, its
        /// phonemes, and openSet.
        constexpr ExportsShape S2P_EXPORTS{true, "phonemes", false, true};

        /// Cache generations of the table parsers. A generation is raised when the table syntax
        /// changes, so cache entries from an older parser no longer match, without explicit
        /// invalidation.
        constexpr char DICT_KIND[] = "s2p-dict-tsv@1";
        constexpr char MAPPING_KIND[] = "s2p-mapping-tsv@1";

        /// Holds the table of the variant, if the variant uses one. Tables come from the shared
        /// resource cache, so a file used by several modules is parsed once.
        using Table = std::variant<std::monostate, std::shared_ptr<const DictionaryTable>,
                                   std::shared_ptr<const MappingTable>>;

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration(std::string variant)
                : ContribConfiguration(S2PApi::API_INTERFACE, std::move(variant),
                                       S2PApi::API_LEVEL) {
            }

            /// Parsed during Acquire rather than when an executive is created, because the
            /// upper specification requires the provider to validate its configuration
            /// during Acquire, and a malformed dictionary must fail the package rather than the
            /// first conversion that uses it.
            Table table;
        };

        /// Reads the single configuration key of these variants.
        ///
        /// Unknown keys are rejected rather than ignored, so a misspelled key produces an error
        /// instead of silently leaving a default in place.
        ///
        /// \return The normalized file path, resolved against the declaration directory if
        /// relative. An empty path if the key is absent and \a required is false. An InvalidFormat
        /// error if the configuration is malformed, or if the key is absent and \a required is
        /// true.
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

        /// Loads the table of a variant through the shared resource cache.
        ///
        /// \return The table for the dict and mapping variants; an empty Table for any other
        /// variant; the load error if the table cannot be loaded.
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

        class Executive
            : public ExecutiveBase<S2PApi::S2PExecutive, S2PApi::S2PStartInput, S2PApi::S2PResult> {
        public:
            Executive(srt::InferenceSpec &spec, Table table)
                : ExecutiveBase(spec), m_table(std::move(table)) {
            }

            ~Executive() {
                finish();
            }

            srt::Expected<void> initialize(const S2PApi::S2PInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), and unloading a Package reaches a
            // running conversion through that forwarding.

        protected:
            Batch runBatch(const S2PApi::S2PStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the parent
                // executive and this call is honored rather than cleared. A request left over from
                // an earlier conversion cancels a batch that the parent executive did not stop;
                // the parent executive detects this case and runs the batch again.
                auto result = std::make_unique<S2PApi::S2PResult>();
                result->phonemes.reserve(input.pronunciations.size());
                for (const auto &pronunciation : input.pronunciations) {
                    // Cancellation is checked between units, so a caller that stops the batch
                    // receives the units completed so far rather than an error.
                    if (stopRequested()) {
                        return result;
                    }
                    result->phonemes.push_back(convert(pronunciation));
                }
                return result;
            }

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
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            explicit Interpreter(std::string variant) : m_variant(std::move(variant)) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto read = readContractExports(spec, S2P_EXPORTS, "exports");
                if (!read) {
                    return read.takeError();
                }
                auto result = std::make_unique<S2PApi::S2PExports>(m_variant);
                result->languages = std::move(read->languages);
                result->phonemes = std::move(read->strings);
                result->openSet = read->openSet;
                return std::unique_ptr<srt::ContribExports>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
                createConfiguration(const srt::ContribSpec &spec) const override {
                auto file = readFile(spec, m_variant != DIRECT);
                if (!file) {
                    return file.takeError();
                }
                auto result = std::make_unique<Configuration>(m_variant);
                // The path is used only to load the table; the direct variant has no table.
                auto table = loadTable(m_variant, file.take());
                if (!table) {
                    return table.takeError();
                }
                result->table = table.take();
                return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
                createImportOptions(const srt::ContribSpec &,
                                    const srt::JsonValue &manifestOptions) const override {
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, m_variant);
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
