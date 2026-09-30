#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/InputRules.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/ResourceCache.h>

#include "Bundle.h"
#include "Decoder.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;

namespace wolf::multig2p {

    namespace {

        constexpr char VARIANT[] = "multig2p-onnx";

        /// Shape of the G2P contract exports: the language pairs the module produces, its
        /// symbols, and openSet.
        constexpr ExportsShape G2P_EXPORTS{true, "symbols", false, true};

        /// The driver backend of this variant, which is also the name of its Runtime Service.
        constexpr char BACKEND[] = "onnx";

        constexpr char BUNDLE_KIND[] = "multig2p-bundle-json@1";
        constexpr char VOCABULARY_KIND[] = "multig2p-vocabulary-json@1";

        /// One contract pair and the bundle language it selects.
        struct MappedLanguage : LanguageMapEntry {
            /// The language id of ref in the bundle, resolved after the bundle is read.
            std::int64_t id = -1;
        };

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(G2PApi::API_INTERFACE, VARIANT, G2PApi::API_LEVEL) {
            }

            std::shared_ptr<const Bundle> bundle;
            std::shared_ptr<const Vocabulary> vocabulary;
            std::vector<MappedLanguage> languageMap;

            /// The directory holding the bundle and its models.
            fs::path directory;

            int maxLength = 48;
        };

        class Executive
            : public ExecutiveBase<G2PApi::G2PExecutive, G2PApi::G2PStartInput, G2PApi::G2PResult> {
        public:
            Executive(srt::InferenceSpec &spec, const Configuration *configuration,
                      std::unique_ptr<Decoder> decoder, std::string languageRef,
                      std::int64_t languageId)
                : ExecutiveBase(spec), m_configuration(configuration),
                  m_decoder(std::move(decoder)), m_languageRef(std::move(languageRef)),
                  m_languageId(languageId) {
            }

            ~Executive() {
                finish();
            }

            srt::Expected<void> initialize(const G2PApi::G2PInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is the path by which a package
            // unload stops a running conversion.

        protected:
            Batch runBatch(const G2PApi::G2PStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the base class
                // and this call is honored rather than cleared. A request left over from an
                // earlier conversion cancels a batch that the base class did not intend to cancel;
                // the base class detects this case and retries the batch.
                auto result = std::make_unique<G2PApi::G2PResult>();
                result->words.resize(input.lyrics.size());

                // No driver is available in this process. The contract defines a word-level error
                // for this case, which allows a chain to fall back instead of the whole language
                // failing to load.
                if (!m_decoder) {
                    for (auto &word : result->words) {
                        word.error = G2PApi::Error::DriverUnavailable;
                    }
                    return result;
                }
                if (input.lyrics.empty()) {
                    return result;
                }

                // The input ordering of the contract applies first, so a word that the contract
                // settles is neither encoded nor counted against the decode budget.
                std::vector<LyricVerdict> verdicts;
                verdicts.reserve(input.lyrics.size());
                std::vector<std::string> convertible;
                std::vector<std::size_t> positions;
                for (std::size_t i = 0; i < input.lyrics.size(); ++i) {
                    verdicts.push_back(classifyLyric(input.lyrics[i]));
                    if (verdicts.back() == LyricVerdict::Skip) {
                        result->words[i].mode = G2PApi::Mode::Skip;
                    } else if (verdicts.back() == LyricVerdict::Invalid) {
                        result->words[i].pronunciation = input.lyrics[i];
                        result->words[i].error = G2PApi::Error::InvalidInput;
                    } else {
                        convertible.push_back(input.lyrics[i]);
                        positions.push_back(i);
                    }
                }
                if (convertible.empty()) {
                    return result;
                }

                auto decoded = m_decoder->run(convertible, m_languageRef, m_languageId,
                                              *m_configuration->vocabulary,
                                              m_configuration->maxLength, stopFlag());
                if (stopRequested()) {
                    // The decoder stopped between decode steps. A partial sequence is not a
                    // pronunciation, so the words are returned empty and the task state records
                    // the cancellation.
                    return result;
                }
                if (!decoded) {
                    // A batch fails as a whole: every word in the batch carries the same error,
                    // rather than the call reporting success with silently empty output.
                    for (const auto position : positions) {
                        result->words[position].error = G2PApi::Error::ModelInferenceFailed;
                    }
                    return result;
                }

                const auto tokens = decoded.take();
                for (std::size_t k = 0; k < positions.size(); ++k) {
                    const auto i = positions[k];
                    auto &word = result->words[i];
                    std::string pronunciation;
                    for (const auto id : tokens[k]) {
                        if (m_configuration->vocabulary->isSpecial(id)) {
                            continue;
                        }
                        auto symbol = m_configuration->vocabulary->phonemeAt(id);
                        if (symbol.empty()) {
                            continue;
                        }
                        if (!pronunciation.empty()) {
                            pronunciation += ' ';
                        }
                        pronunciation += symbol;
                    }
                    if (pronunciation.empty()) {
                        word.error = G2PApi::Error::PhonemeGenerationFailed;
                        continue;
                    }
                    word.pronunciation = pronunciation;
                    word.candidates.push_back(pronunciation);
                    word.hitSource = G2PApi::HitSource::Model;
                }
                return result;
            }

        private:
            const Configuration *m_configuration;
            std::unique_ptr<Decoder> m_decoder;
            std::string m_languageRef;
            std::int64_t m_languageId;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto read = readContractExports(spec, G2P_EXPORTS, "exports");
                if (!read) {
                    return read.takeError();
                }
                auto result = std::make_unique<G2PApi::G2PExports>(VARIANT);
                result->languages = std::move(read->languages);
                result->symbols = std::move(read->strings);
                result->openSet = read->openSet;
                return std::unique_ptr<srt::ContribExports>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
                createConfiguration(const srt::ContribSpec &spec) const override {
                const auto &value = spec.manifestConfiguration();
                if (!value.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the multig2p configuration must be an object");
                }
                auto result = std::make_unique<Configuration>();
                std::vector<LanguageMapEntry> mapped;
                for (const auto &[key, item] : value.toObject()) {
                    if (key == "languageMap") {
                        auto languageMap = readLanguageMap(item);
                        if (!languageMap) {
                            return languageMap.takeError();
                        }
                        mapped = languageMap.take();
                    } else if (key == "maxLen") {
                        if (!item.isInt() || item.toInt() < 1) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "maxLen must be a positive integer");
                        }
                        result->maxLength = static_cast<int>(item.toInt());
                    } else if (key == "beamSize" || key == "topK") {
                        // Both keys widen the search, which only beam decoding performs. This
                        // build decodes greedily, so accepting a wider setting and silently
                        // ignoring it would misreport the behavior of the module.
                        if (!item.isInt() || item.toInt() < 1) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              key + " must be a positive integer");
                        }
                        if (item.toInt() != 1) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              key + " above 1 requires beam search, which this "
                                                    "build does not implement");
                        }
                    } else if (key == "lengthPenalty") {
                        if (!item.isDouble() && !item.isInt()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "lengthPenalty must be a number");
                        }
                        if (item.toDouble() != 0.0) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              "lengthPenalty only affects beam search, which this "
                                              "build does not implement");
                        }
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "unknown multig2p configuration key: " + key);
                    }
                }
                if (mapped.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the multig2p configuration needs a languageMap");
                }
                if (auto reconciled = reconcileLanguageMap(spec, mapped); !reconciled) {
                    return reconciled.takeError();
                }
                for (auto &entry : mapped) {
                    MappedLanguage language;
                    static_cast<LanguageMapEntry &>(language) = std::move(entry);
                    result->languageMap.push_back(std::move(language));
                }

                // The bundle is located beside the declaration, so no key refers to it: each
                // declaration has exactly one bundle, and a path key would only restate that
                // location.
                result->directory = spec.declarationPath().parent_path();
                auto &cache = ResourceCache::instance();
                auto bundle = cache.acquire<Bundle>(
                    result->directory / "bundle.json", BUNDLE_KIND, VARIANT,
                    std::function(
                        [](const fs::path &path) -> srt::Expected<std::shared_ptr<const Bundle>> {
                            auto parsed = Bundle::load(path);
                            if (!parsed) {
                                return parsed.takeError();
                            }
                            return std::make_shared<const Bundle>(parsed.take());
                        }));
                if (!bundle) {
                    return bundle.takeError();
                }
                result->bundle = bundle.take();

                auto vocabulary = cache.acquire<Vocabulary>(
                    result->directory / "vocabulary.json", VOCABULARY_KIND, VARIANT,
                    std::function([](const fs::path &path)
                                      -> srt::Expected<std::shared_ptr<const Vocabulary>> {
                        auto parsed = Vocabulary::load(path);
                        if (!parsed) {
                            return parsed.takeError();
                        }
                        return std::make_shared<const Vocabulary>(parsed.take());
                    }));
                if (!vocabulary) {
                    return vocabulary.takeError();
                }
                result->vocabulary = vocabulary.take();

                // The ids are resolved once here rather than per executive, because the language
                // id of a reference is a property of the bundle, and an unknown reference is a
                // declaration error.
                for (auto &entry : result->languageMap) {
                    entry.id = result->bundle->languageId(entry.ref);
                    if (entry.id < 0) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "languageMap names " + entry.ref +
                                              ", which the bundle does not list");
                    }
                }
                return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
            }

            srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
                createImportOptions(const srt::ContribSpec &,
                                    const srt::JsonValue &manifestOptions) const override {
                if (auto checked = wolf::requireNoImportOptions(manifestOptions, VARIANT);
                    !checked) {
                    return checked.takeError();
                }
                return std::unique_ptr<srt::ContribImportOptions>(
                    new G2PApi::G2PImportOptions(VARIANT));
            }

            srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
                createInference(srt::InferenceSpec &spec, const srt::ContribImportOptions &,
                                const srt::InferenceRuntimeOptions &runtimeOptions) override {
                const auto *configuration =
                    static_cast<const Configuration *>(spec.configuration());
                const auto &binding =
                    static_cast<const G2PApi::G2PRuntimeOptions &>(runtimeOptions).binding;

                const MappedLanguage *mapped = nullptr;
                for (const auto &entry : configuration->languageMap) {
                    if (entry.pair == binding) {
                        mapped = &entry;
                        break;
                    }
                }
                if (mapped == nullptr) {
                    // No default language exists: an executive is bound to one pair, so a pair
                    // that the language map does not contain is an error rather than a case for
                    // substitution.
                    return srt::Error(srt::Error::FeatureNotSupported,
                                      "this bundle maps no language to " + binding.language + "/" +
                                          binding.scheme);
                }

                // The host owns the backend and registers it as a Runtime Service. The absence of
                // the service is a property of the installation rather than of the package, so the
                // executive reports a per-word error instead of failing the load.
                auto *service = spec.package().synthUnit().runtimeService(
                    ds::InferenceDriverPlugin::IID, BACKEND);
                if (service == nullptr) {
                    return std::unique_ptr<srt::InferenceExecutive>(
                        new Executive(spec, configuration, nullptr, mapped->ref, mapped->id));
                }

                auto decoder = Decoder::open(*service->as<ds::InferenceDriver>(),
                                             *configuration->bundle, configuration->directory);
                if (!decoder) {
                    // The driver is available but the models fail to open, so the fault lies in
                    // the package rather than in the installation.
                    return decoder.takeError();
                }
                return std::unique_ptr<srt::InferenceExecutive>(
                    new Executive(spec, configuration, decoder.take(), mapped->ref, mapped->id));
            }
        };

    }

    class MultiG2PPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != G2PApi::API_INTERFACE || level != G2PApi::API_LEVEL ||
                variant != VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported multig2p contract or variant");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new Interpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::multig2p::MultiG2PPlugin)
