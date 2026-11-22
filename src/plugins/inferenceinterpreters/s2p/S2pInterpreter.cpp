#include "S2pInterpreter.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <synthrt/SVS/InferenceInterpreter.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>
#include <wolf/Support/ResourceCache.h>

#include <executiveutil/ExecutiveBase.h>
#include <linguistutil/ContractValues.h>
#include <linguistutil/ManifestValues.h>

#include "S2pTables.h"

namespace fs = std::filesystem;
namespace S2pApi = wolf::Api::S2p::L1;

namespace wolf::s2p {

    namespace {

        // Exports of the S2P contract: the language/scheme pairs that the module consumes, its
        // phonemes, and openSet.
        constexpr ExportsShape S2P_EXPORTS{true, "phonemes", false, true};

        // Cache generations of the table parsers. A generation is raised when the table syntax
        // changes, so cache entries from an older parser no longer match, without explicit
        // invalidation.
        constexpr char DICT_KIND[] = "s2p-dict-tsv@1";
        constexpr char MAPPING_KIND[] = "s2p-mapping-tsv@1";

        // Holds the table of the variant, if the variant uses one. Tables come from the shared
        // resource cache, so a file used by several modules is parsed once.
        using Table = std::variant<std::monostate, std::shared_ptr<const DictionaryTable>,
                                   std::shared_ptr<const MappingTable>>;

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration(std::string variant)
                : ContribConfiguration({S2pApi::Contract.interfaceId(), S2pApi::Contract.level(),
                                        std::move(variant)}) {
            }

            // Parsed during Acquire rather than when an executive is created, because the
            // upper specification requires the provider to validate its configuration
            // during Acquire, and a malformed dictionary must fail the package rather than the
            // first conversion that uses it.
            Table table;
        };

        // Reads the single configuration key of these variants.
        //
        // Unknown keys are rejected rather than ignored, so a misspelled key produces an error
        // instead of silently leaving a default in place.
        //
        // \return The normalized file path, resolved against the declaration directory if
        // relative. An empty path if the key is absent and \a required is false. An InvalidFormat
        // error if the configuration is malformed, or if the key is absent and \a required is
        // true.
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

        // Loads the table of a variant through the shared resource cache.
        //
        // \return The table for the dict and mapping variants. An empty Table for any other
        // variant. The load error if the table cannot be loaded.
        srt::Expected<Table> loadTable(const std::string &variant, const fs::path &file) {
            auto &cache = ResourceCache::instance();
            if (variant == DictTriplet.variant()) {
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
            if (variant == MappingTriplet.variant()) {
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
            : public ExecutiveBase<S2pApi::Executive, S2pApi::StartInput, S2pApi::Result> {
        public:
            Executive(srt::InferenceSpec &spec, Table table)
                : ExecutiveBase(spec), m_table(std::move(table)) {
            }

            ~Executive() {
                (void) quit();
                (void) wait();
            }

            srt::Expected<void> initialize(const S2pApi::InitArgs &) override {
                return {};
            }

            // See ExecutiveBase: quit() and wait() are deliberately not overridden.

        protected:
            Batch runBatch(const S2pApi::StartInput &input) override {
                // See ExecutiveBase: a stop request on entry cancels this batch.
                auto result = std::make_unique<S2pApi::Result>();
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

    }

    S2pInterpreter::S2pInterpreter(std::string variant) : m_variant(std::move(variant)) {
    }

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        S2pInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto read = readContractExports(spec, S2P_EXPORTS, "exports");
        if (!read) {
            return read.takeError();
        }
        auto result = std::make_unique<S2pApi::Exports>();
        result->preservesSymbols = m_variant == DirectTriplet.variant();
        result->languages = std::move(read->languages);
        result->phonemes = std::move(read->strings);
        result->openSet = read->openSet;
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        S2pInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        auto file = readFile(spec, m_variant != DirectTriplet.variant());
        if (!file) {
            return file.takeError();
        }
        auto result = std::make_unique<Configuration>(m_variant);
        // The path is used only to load the table. The direct variant has no table.
        auto table = loadTable(m_variant, file.take());
        if (!table) {
            return table.takeError();
        }
        result->table = table.take();
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        S2pInterpreter::createImportOptions(const srt::ContribSpec &,
                                            const srt::JsonValue &manifestOptions) const {
        if (auto checked = wolf::requireNoImportOptions(manifestOptions, m_variant); !checked) {
            return checked.takeError();
        }
        return std::unique_ptr<srt::ContribImportOptions>(new S2pApi::ImportOptions());
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        S2pInterpreter::createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                        const srt::InferenceRuntimeOptions &) {
        auto configuration = static_cast<const Configuration *>(spec.configuration());
        if (configuration == nullptr) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the S2P module was imported without its configuration");
        }
        return std::unique_ptr<srt::InferenceExecutive>(new Executive(spec, configuration->table));
    }

}
