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
#include <wolf/Support/ExecutiveTask.h>
#include <wolf/Support/InputRules.h>
#include <wolf/Support/ManifestValues.h>

#include "PinyinEngines.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;

namespace wolf::pinyin {

    namespace {

        constexpr char VARIANT[] = "algo-pinyin";

        /// The highest configuration generation this build reads. A declaration above it is
        /// refused rather than read leniently.
        constexpr int FORMAT_VERSION = 1;

        /// One contract pair and the engine that serves it.
        struct MappedLanguage {
            Api::Common::L1::LanguageScheme pair;
            std::string ref;
        };

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(G2PApi::API_INTERFACE, VARIANT, G2PApi::API_LEVEL) {
            }

            fs::path dictRoot;
            std::vector<MappedLanguage> languageMap;

            /// The claim on the process wide dictionary root, released when this configuration is.
            /// Holding it here is what ties the claim to the transaction: a load that fails after
            /// createConfiguration drops this object, and the root goes back with it.
            RootReservation reservation;
        };

        /// Splits a UTF-8 string into its code points.
        ///
        /// The engine looks up one character at a time, so a word has to arrive as characters. A
        /// malformed byte becomes its own element and simply fails to convert.
        std::vector<std::string> splitCharacters(const std::string &word) {
            std::vector<std::string> characters;
            for (std::size_t i = 0; i < word.size();) {
                const auto lead = static_cast<unsigned char>(word[i]);
                std::size_t length = 1;
                if ((lead & 0xE0) == 0xC0) {
                    length = 2;
                } else if ((lead & 0xF0) == 0xE0) {
                    length = 3;
                } else if ((lead & 0xF8) == 0xF0) {
                    length = 4;
                }
                length = std::min(length, word.size() - i);
                characters.push_back(word.substr(i, length));
                i += length;
            }
            return characters;
        }

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
                // The refs name engines compiled into this plugin, so an unreachable one is a
                // declaration error rather than something to discover at conversion time.
                if (!PinyinEngineRegistry::isKnownRef(entry.ref)) {
                    return srt::Error(srt::Error::FeatureNotSupported,
                                      position + " names an unknown engine: " + entry.ref);
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
        ///
        /// One is the contract face and the other the implementation face; neither can be derived
        /// from the other, so both are written and reconciled here.
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
            Executive(srt::InferenceSpec &spec, std::unique_ptr<Engine> engine)
                : G2PExecutive(spec), m_engine(std::move(engine)),
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
                // The contract's input ordering comes first: a word it already ruled on never
                // reaches the engine, and it also breaks the run of convertible words it sits
                // between — the phrase tables read adjacency, and two words separated by a word
                // the contract ruled on are not adjacent in the song.
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
                    for (auto &character : splitCharacters(input.lyrics[i])) {
                        characters.push_back(std::move(character));
                    }
                }
                offsets.push_back(characters.size());

                auto result = std::make_unique<G2PApi::G2PResult>();
                // The convertible runs go to the engine as runs, because adjacency is part of what
                // it reads — two characters read as neighbours and the same two read apart are
                // different readings. Cancellation is answered before the first call rather than
                // inside it; a stop that lands during the runs is consumed by the next start, which
                // is what the parent expects of a cancelled batch.
                if (m_stopRequested) {
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
            /// Joins one word's characters back together.
            ///
            /// A word converts as a whole or not at all: a partially converted word would have to
            /// invent a reading for the characters that failed, so it comes back empty and the
            /// chain's fallback step decides what the word becomes.
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
                // Alternatives only mean something for a single character: across a word they
                // would be a cross product this contract has no way to express.
                //
                // The engine's alternatives come from its per character table while the reading
                // itself may come from a phrase, so the two disagree exactly when a phrase
                // decided the reading. The contract has the pronunciation lead its candidates,
                // so it is put there and the rest keep their order.
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
                                      "the pinyin configuration must be an object");
                }
                auto result = std::make_unique<Configuration>();
                bool hasFormatVersion = false;
                fs::path dictRoot;
                for (const auto &[key, item] : value.toObject()) {
                    if (key == "formatVersion") {
                        if (!item.isInt() || item.toInt() < 1) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "formatVersion must be a positive integer");
                        }
                        if (item.toInt() > FORMAT_VERSION) {
                            return srt::Error(srt::Error::FeatureNotSupported,
                                              "this configuration declares format version " +
                                                  std::to_string(item.toInt()) + ", above the " +
                                                  std::to_string(FORMAT_VERSION) +
                                                  " this build reads; upgrade wolf to load it");
                        }
                        hasFormatVersion = true;
                    } else if (key == "dictRoot") {
                        if (!item.isString() || item.toString().empty()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "dictRoot must be a non-empty string");
                        }
                        dictRoot = pathFromManifest(item.toString());
                    } else if (key == "languageMap") {
                        auto languageMap = readLanguageMap(item);
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
                                      "the pinyin configuration needs a formatVersion");
                }
                if (dictRoot.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the pinyin configuration needs a dictRoot");
                }
                if (result->languageMap.empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the pinyin configuration needs a languageMap");
                }
                if (auto reconciled = reconcile(spec, result->languageMap); !reconciled) {
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

                // Claimed here rather than when an executive appears, because a clash between two
                // roots is a fact about the packages and belongs in the load that carries them.
                // The claim is kept on the configuration so that it lasts exactly as long as this
                // module instance does.
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
