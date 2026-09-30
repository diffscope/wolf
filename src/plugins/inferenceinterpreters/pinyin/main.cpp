#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/InputRules.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/Utf8.h>

#include "PinyinEngines.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;

namespace wolf::pinyin {

    namespace {

        constexpr char VARIANT[] = "algo-pinyin";

        /// Exports of the G2P contract: the language/scheme pairs that the module produces, its
        /// symbols, and openSet.
        constexpr ExportsShape G2P_EXPORTS{true, "symbols", false, true};

        /// Highest configuration format version supported by this build. A declaration with a
        /// higher version is rejected rather than read leniently.
        constexpr int FORMAT_VERSION = 1;

        /// One contract pair and the engine that converts it.
        using MappedLanguage = LanguageMapEntry;

        /// Checks that \a ref names an engine compiled into this plugin. An unknown ref is a
        /// declaration error and is reported at load time rather than at conversion time.
        ///
        /// \return Success if the ref is known; otherwise a FeatureNotSupported error.
        srt::Expected<void> checkEngineRef(const std::string &position, const std::string &ref) {
            if (!PinyinEngineRegistry::isKnownRef(ref)) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  position + " names an unknown engine: " + ref);
            }
            return {};
        }

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(G2PApi::API_INTERFACE, VARIANT, G2PApi::API_LEVEL) {
            }

            fs::path dictRoot;
            std::vector<MappedLanguage> languageMap;

            /// Claim on the process-wide dictionary root, released together with this
            /// configuration. Holding the claim here ties it to the load transaction: a load that
            /// fails after createConfiguration destroys this object, which releases the root.
            RootReservation reservation;
        };

        class Executive
            : public ExecutiveBase<G2PApi::G2PExecutive, G2PApi::G2PStartInput, G2PApi::G2PResult> {
        public:
            Executive(srt::InferenceSpec &spec, std::unique_ptr<Engine> engine)
                : ExecutiveBase(spec), m_engine(std::move(engine)) {
            }

            ~Executive() {
                finish();
            }

            srt::Expected<void> initialize(const G2PApi::G2PInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), and unloading a Package reaches a
            // running conversion through that forwarding.

        protected:
            Batch runBatch(const G2PApi::G2PStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the parent
                // executive and this call is honored rather than cleared. A request left over from
                // an earlier conversion cancels a batch that the parent executive did not stop;
                // the parent executive detects this case and runs the batch again.
                // The input rules of the contract are applied first: a word that the rules classify
                // as skipped or invalid never reaches the engine, and it also splits the run of
                // convertible words around it. The phrase tables depend on adjacency, and two
                // words separated by such a word are not adjacent in the song.
                std::vector<LyricVerdict> verdicts;
                verdicts.reserve(input.lyrics.size());
                for (const auto &lyric : input.lyrics) {
                    verdicts.push_back(classifyLyric(lyric));
                }

                std::vector<std::string> characters;
                std::vector<std::size_t> offsets;
                offsets.reserve(input.lyrics.size() + 1);
                for (std::size_t i = 0; i < input.lyrics.size(); ++i) {
                    offsets.push_back(characters.size());
                    if (verdicts[i] != LyricVerdict::Accept) {
                        continue;
                    }
                    // The engine looks up one character at a time, so a word must be split into
                    // characters. An invalid byte becomes a separate element and fails to convert.
                    for (auto &character : splitUtf8(input.lyrics[i])) {
                        characters.push_back(std::move(character));
                    }
                }
                offsets.push_back(characters.size());

                auto result = std::make_unique<G2PApi::G2PResult>();
                // Each convertible run is passed to the engine as a whole, because the engine uses
                // adjacency: the same two characters can receive different readings when converted
                // together and when converted separately. Cancellation is checked before the
                // first call rather than during the calls. A stop request that arrives during the
                // runs is consumed by the next start, as the parent executive expects of a
                // canceled batch.
                if (stopRequested()) {
                    return result;
                }
                std::vector<CharacterResult> converted;
                converted.reserve(characters.size());
                for (std::size_t begin = 0; begin < input.lyrics.size();) {
                    if (verdicts[begin] != LyricVerdict::Accept) {
                        ++begin;
                        continue;
                    }
                    std::size_t end = begin;
                    while (end < input.lyrics.size() && verdicts[end] == LyricVerdict::Accept) {
                        ++end;
                    }
                    const std::vector<std::string> run(characters.begin() + offsets[begin],
                                                       characters.begin() + offsets[end]);
                    const auto part = m_engine->convert(run);
                    // The assembly requires exactly one reading per character, because the
                    // offsets that split the readings into words are computed from the input. A
                    // different reading count would assign every word after the affected run the
                    // readings of neighboring words, and the last word would be read past the end
                    // of the readings. The engine is third-party code, so the count is checked.
                    if (part.size() != run.size()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "the pinyin engine returned " +
                                              std::to_string(part.size()) + " readings for " +
                                              std::to_string(run.size()) + " characters");
                    }
                    converted.insert(converted.end(), part.begin(), part.end());
                    begin = end;
                }
                result->words.reserve(input.lyrics.size());
                for (std::size_t i = 0; i < input.lyrics.size(); ++i) {
                    switch (verdicts[i]) {
                        case LyricVerdict::Skip: {
                            G2PApi::G2PWordOutput word;
                            word.mode = G2PApi::Mode::Skip;
                            result->words.push_back(std::move(word));
                            break;
                        }
                        case LyricVerdict::Invalid: {
                            G2PApi::G2PWordOutput word;
                            word.pronunciation = input.lyrics[i];
                            word.error = G2PApi::Error::InvalidInput;
                            result->words.push_back(std::move(word));
                            break;
                        }
                        case LyricVerdict::Accept:
                            result->words.push_back(
                                assemble(converted, offsets[i], offsets[i + 1]));
                            break;
                    }
                }
                return result;
            }

        private:
            /// Joins the character readings of one word.
            ///
            /// A word is converted completely or not at all. A partially converted word would
            /// require an invented reading for the failed characters, so such a word is returned
            /// without a pronunciation, and the fallback step of the chain determines its result.
            ///
            /// \return A word with the error PhonemeGenerationFailed if the word has no characters
            /// or if any character has no reading; otherwise the converted word with its
            /// candidates.
            static G2PApi::G2PWordOutput assemble(const std::vector<CharacterResult> &converted,
                                                  std::size_t begin, std::size_t end) {
                G2PApi::G2PWordOutput word;
                word.mode = G2PApi::Mode::Convert;
                if (begin == end) {
                    word.error = G2PApi::Error::PhonemeGenerationFailed;
                    return word;
                }
                for (auto i = begin; i < end; ++i) {
                    if (converted[i].pronunciation.empty()) {
                        word.error = G2PApi::Error::PhonemeGenerationFailed;
                        return word;
                    }
                    if (!word.pronunciation.empty()) {
                        word.pronunciation += ' ';
                    }
                    word.pronunciation += converted[i].pronunciation;
                }
                word.hitSource = G2PApi::HitSource::Rule;
                // Alternatives are defined only for a single character. Across a word they would
                // form a cross product that this contract cannot express.
                //
                // The alternatives of the engine come from its per-character table, while the
                // reading itself may come from a phrase table, so the two differ exactly when a
                // phrase determined the reading. The contract requires the pronunciation to be the
                // first candidate, so the pronunciation is inserted first and the other candidates
                // keep their order.
                word.candidates.push_back(word.pronunciation);
                if (end - begin == 1) {
                    for (const auto &candidate : converted[begin].candidates) {
                        if (candidate != word.pronunciation) {
                            word.candidates.push_back(candidate);
                        }
                    }
                }
                return word;
            }

            std::unique_ptr<Engine> m_engine;
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
                                      "the pinyin configuration must be an object");
                }
                auto result = std::make_unique<Configuration>();
                bool hasFormatVersion = false;
                fs::path dictRoot;
                for (const auto &[key, item] : value.toObject()) {
                    if (key == "formatVersion") {
                        if (auto version = readFormatVersion(item, FORMAT_VERSION); !version) {
                            return version.takeError();
                        }
                        hasFormatVersion = true;
                    } else if (key == "dictRoot") {
                        if (!item.isString() || item.toString().empty()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "dictRoot must be a non-empty string");
                        }
                        dictRoot = pathFromManifest(item.toString());
                    } else if (key == "languageMap") {
                        auto languageMap = readLanguageMap(item, checkEngineRef);
                        if (!languageMap) {
                            return languageMap.takeError();
                        }
                        result->languageMap = languageMap.take();
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "unknown pinyin configuration key: " + key);
                    }
                }
                if (!hasFormatVersion) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the pinyin configuration requires a formatVersion");
                }
                if (dictRoot.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the pinyin configuration requires a dictRoot");
                }
                if (result->languageMap.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the pinyin configuration requires a languageMap");
                }
                if (auto reconciled = reconcileLanguageMap(spec, result->languageMap);
                    !reconciled) {
                    return reconciled.takeError();
                }

                if (dictRoot.is_relative()) {
                    dictRoot = spec.declarationPath().parent_path() / dictRoot;
                }
                std::error_code ec;
                result->dictRoot = fs::weakly_canonical(dictRoot.lexically_normal(), ec);
                if (ec) {
                    result->dictRoot = dictRoot.lexically_normal();
                }
                if (!fs::is_directory(result->dictRoot, ec)) {
                    return srt::Error(srt::Error::FileNotFound,
                                      "the pinyin dictionary root is missing: " +
                                          stdc::path::to_utf8(result->dictRoot));
                }

                // The root is claimed here rather than when an executive is created, because a
                // conflict between two roots is a property of the packages and must be reported by
                // the load that contains them. The configuration holds the claim so that the claim
                // lasts exactly as long as this module instance.
                auto reserved = PinyinEngineRegistry::instance().reserveRoot(result->dictRoot);
                if (!reserved) {
                    return reserved.takeError();
                }
                result->reservation = reserved.take();
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
                for (const auto &entry : configuration->languageMap) {
                    if (entry.pair == binding) {
                        auto engine = PinyinEngineRegistry::instance().createEngine(
                            configuration->dictRoot, entry.ref);
                        if (!engine) {
                            return engine.takeError();
                        }
                        return std::unique_ptr<srt::InferenceExecutive>(
                            new Executive(spec, engine.take()));
                    }
                }
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "this pinyin module maps no engine to " + binding.language + "/" +
                                      binding.scheme);
            }
        };

    }

    class PinyinPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != G2PApi::API_INTERFACE || level != G2PApi::API_LEVEL ||
                variant != VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported pinyin contract or variant");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new Interpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::pinyin::PinyinPlugin)
