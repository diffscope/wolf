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
#include <wolf/Support/ExecutiveTask.h>
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

        /// The driver backend this variant runs on, and the service name it is registered under.
        constexpr char BACKEND[] = "onnx";

        constexpr char BUNDLE_KIND[] = "multig2p-bundle-json@1";
        constexpr char VOCABULARY_KIND[] = "multig2p-vocabulary-json@1";

        /// One contract pair and the bundle language it selects.
        struct MappedLanguage {
            Api::Common::L1::LanguageScheme pair;
            std::string ref;
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

        srt::Expected<std::vector<MappedLanguage>> readLanguageMap(const srt::JsonValue &value) {
            if (!value.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "languageMap must be an array");
            }
            std::vector<MappedLanguage> entries;
            const auto &array = value.toArray();
            entries.reserve(array.size());
            for (std::size_t i = 0; i < array.size(); ++i) {
                const auto position = "languageMap entry " + std::to_string(i);
                if (!array[i].isObject()) {
                    return srt::Error(srt::Error::InvalidFormat, position + " must be an object");
                }
                MappedLanguage entry;
                for (const auto &[key, item] : array[i].toObject()) {
                    if (!item.isString() || item.toString().empty()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " " + key + " must be a non-empty string");
                    }
                    if (key == "language") {
                        entry.pair.language = item.toString();
                    } else if (key == "scheme") {
                        entry.pair.scheme = item.toString();
                    } else if (key == "ref") {
                        entry.ref = item.toString();
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " has an unknown key: " + key);
                    }
                }
                if (entry.pair.language.empty() || entry.pair.scheme.empty() || entry.ref.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      position + " needs a language, a scheme and a ref");
                }
                for (const auto &earlier : entries) {
                    if (earlier.pair == entry.pair) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " repeats a pair already mapped");
                    }
                }
                entries.push_back(std::move(entry));
            }
            if (entries.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "languageMap must not be empty");
            }
            return entries;
        }

        /// Checks that the declared exports and the mapping describe the same set of pairs.
        srt::Expected<void> reconcile(const srt::ContribSpec &spec,
                                      const std::vector<MappedLanguage> &languageMap) {
            const auto &exports = spec.manifestExports();
            if (!exports.isObject() || exports.toObject().count("languages") == 0) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "this variant requires exports languages, which nothing can "
                                  "derive from languageMap");
            }
            auto declared =
                readLanguageSchemes(exports.toObject().at("languages"), "exports languages");
            if (!declared) {
                return declared.takeError();
            }
            const auto pairs = declared.take();
            if (pairs.size() != languageMap.size()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "exports languages and languageMap describe different pairs");
            }
            for (const auto &entry : languageMap) {
                bool found = false;
                for (const auto &pair : pairs) {
                    if (pair == entry.pair) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "languageMap maps " + entry.pair.language + "/" +
                                          entry.pair.scheme + ", which exports languages omits");
                }
            }
            return {};
        }

        class Executive : public G2PApi::G2PExecutive {
        public:
            Executive(srt::InferenceSpec &spec, const Configuration *configuration,
                      std::unique_ptr<Decoder> decoder, std::string languageRef,
                      std::int64_t languageId)
                : G2PExecutive(spec), m_configuration(configuration), m_decoder(std::move(decoder)),
                  m_languageRef(std::move(languageRef)), m_languageId(languageId),
                  m_task([this](const G2PApi::G2PStartInput &input) { return runBatch(input); },
                         [this] { return m_stopRequested.exchange(false); }) {
            }

            ~Executive() {
                (void) m_task.waitForFinished();
            }

            srt::Expected<void> initialize(const G2PApi::G2PInitArgs &) override {
                return {};
            }

            srt::Expected<std::unique_ptr<G2PApi::G2PResult>>
                start(const G2PApi::G2PStartInput &input) override {
                return m_task.run(input);
            }

        private:
            srt::Expected<std::unique_ptr<G2PApi::G2PResult>>
                runBatch(const G2PApi::G2PStartInput &input) {
                // A request pending on entry cancels this batch. A stop is consumed only after a
                // run, so one that landed between the parent's check and this call is honored
                // rather than cleared away; one left over from an earlier conversion cancels a
                // batch the parent never meant to cancel, which the parent recognizes and retries.
                auto result = std::make_unique<G2PApi::G2PResult>();
                result->words.resize(input.lyrics.size());

                // No driver in this process. The contract has a word level way to say so, which is
                // what lets a chain fall back instead of the whole language failing to load.
                if (!m_decoder) {
                    for (auto &word : result->words) {
                        word.error = G2PApi::Error::DriverUnavailable;
                    }
                    return result;
                }
                if (input.lyrics.empty()) {
                    return result;
                }

                // The contract's input ordering first, so a word it ruled on is neither encoded
                // nor counted against the decode budget.
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
                                              m_configuration->maxLength, m_stopRequested);
                if (m_stopRequested) {
                    // Stopped between decode steps. The partial batch is not a pronunciation, so
                    // the words come back empty and the state says why.
                    return result;
                }
                if (!decoded) {
                    // A failed batch is a failed batch: every word in it carries the same error
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

        public:
            srt::Expected<void> startAsync(std::shared_ptr<const G2PApi::G2PStartInput> input,
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
            // a running conversion. Overriding them to return {} — as this did — silently disabled
            // that.

        private:
            const Configuration *m_configuration;
            std::unique_ptr<Decoder> m_decoder;
            std::string m_languageRef;
            std::int64_t m_languageId;
            std::atomic_bool m_stopRequested = false;

            /// Last, so it is the first member destroyed and its wait runs before the
            /// fields the body reads are gone.
            ExecutiveTask<G2PApi::G2PStartInput, G2PApi::G2PResult> m_task;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            srt::Expected<std::unique_ptr<srt::ContribExports>>
                createExports(const srt::ContribSpec &spec) const override {
                auto result = std::make_unique<G2PApi::G2PExports>(VARIANT);
                const auto &declared = spec.manifestExports();
                if (!declared.isNull() && !declared.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat, "exports must be an object");
                }
                if (declared.isObject()) {
                    const auto &object = declared.toObject();
                    if (auto checked = wolf::rejectUnknownKeys(
                            object, {"languages", "symbols", "openSet"}, "exports");
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
                    if (const auto it = object.find("symbols"); it != object.end()) {
                        auto symbols = readStringSet(
                            it->second, spec.declarationPath().parent_path(), "exports symbols");
                        if (!symbols) {
                            return symbols.takeError();
                        }
                        result->symbols = symbols.take();
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
                const auto &value = spec.manifestConfiguration();
                if (!value.isObject()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the multig2p configuration must be an object");
                }
                auto result = std::make_unique<Configuration>();
                for (const auto &[key, item] : value.toObject()) {
                    if (key == "languageMap") {
                        auto languageMap = readLanguageMap(item);
                        if (!languageMap) {
                            return languageMap.takeError();
                        }
                        result->languageMap = languageMap.take();
                    } else if (key == "maxLen") {
                        if (!item.isInt() || item.toInt() < 1) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "maxLen must be a positive integer");
                        }
                        result->maxLength = static_cast<int>(item.toInt());
                    } else if (key == "beamSize" || key == "topK") {
                        // Both widen the search, and both are what beam decoding is for. This
                        // build decodes greedily, so accepting a wider setting and quietly
                        // ignoring it would misreport what the module does.
                        if (!item.isInt() || item.toInt() < 1) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              key + " must be a positive integer");
                        }
                        if (item.toInt() != 1) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              key + " above 1 asks for beam search, which this "
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
                if (result->languageMap.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the multig2p configuration needs a languageMap");
                }
                if (auto reconciled = reconcile(spec, result->languageMap); !reconciled) {
                    return reconciled.takeError();
                }

                // The bundle sits beside the declaration, so there is no key pointing at it: one
                // declaration, one bundle, and a path key would only be a second way to say so.
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

                // Resolved once here rather than per executive: which language id a reference
                // stands for is a fact about the bundle, and an unknown reference is a
                // declaration error.
                for (auto &entry : result->languageMap) {
                    entry.id = result->bundle->languageId(entry.ref);
                    if (entry.id < 0) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "languageMap names " + entry.ref +
                                              ", which the bundle does not carry");
                    }
                }
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
                    // No default language: an executive is bound to one pair, so a pair the bundle
                    // does not carry is a failure rather than something to substitute for.
                    return srt::Error(srt::Error::FeatureNotSupported,
                                      "this bundle maps no language to " + binding.language + "/" +
                                          binding.scheme);
                }

                // The host owns the backend and registers it as a Runtime Service. Its absence is
                // a property of the installation, not of the package, so it degrades per word
                // instead of failing the load.
                auto *service = spec.package().synthUnit().runtimeService(
                    ds::InferenceDriverPlugin::IID, BACKEND);
                if (service == nullptr) {
                    return std::unique_ptr<srt::InferenceExecutive>(
                        new Executive(spec, configuration, nullptr, mapped->ref, mapped->id));
                }

                auto decoder = Decoder::open(*service->as<ds::InferenceDriver>(),
                                             *configuration->bundle, configuration->directory);
                if (!decoder) {
                    // The driver is here and the models still would not open, which makes this a
                    // fault of the package rather than of the installation.
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
