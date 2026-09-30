#include <algorithm>
#include <map>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <stdcorelib/path.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/InputRules.h>
#include <wolf/Support/ManifestValues.h>
#include <wolf/Support/ResourceCache.h>
#include <wolf/Support/Verifier.h>

#include "ChainTables.h"

namespace fs = std::filesystem;
namespace G2PApi = wolf::Api::G2P::L1;

namespace wolf::chain {

    namespace {

        constexpr char VARIANT[] = "pipe-chain";

        /// Shape of the G2P contract exports: the language pairs the module produces, its
        /// symbols, and openSet.
        constexpr ExportsShape G2P_EXPORTS{true, "symbols", false, true};

        /// The highest configuration generation this build reads.
        constexpr int FORMAT_VERSION = 1;

        /// Maximum number of steps. A longer chain is rejected as a declaration error.
        constexpr std::size_t MAX_STEPS = 50;

        constexpr char DICT_KIND[] = "g2p-dict-tsv@1";

        enum class StepKind {
            Verify,
            Dict,
            Format,
            Model,
            Fallback,
        };

        /// One configured step. The step kinds use disjoint fields, but there are five kinds with
        /// at most three values each, so a single struct is easier to read than a class
        /// hierarchy.
        struct Step {
            StepKind kind = StepKind::Verify;

            // verify
            std::shared_ptr<const Verifier> verifier;

            // dict
            std::shared_ptr<const ChainDictionary> dictionary;

            // format
            bool lowercase = false;
            bool stripTrailing = false;
            bool addSpaces = false;

            // model
            std::string role;
            int batchSize = 0;

            // fallback
            bool useOriginal = true;
            std::string defaultPronunciation;
        };

        class Configuration : public srt::ContribConfiguration {
        public:
            Configuration()
                : ContribConfiguration(G2PApi::API_INTERFACE, VARIANT, G2PApi::API_LEVEL) {
            }

            std::vector<Step> steps;
        };

        /// Working state of one word during a pass through the chain.
        struct ChainWord {
            std::string lyric;
            std::string text;
            G2PApi::Mode mode = G2PApi::Mode::Convert;
            std::string pronunciation;
            std::vector<std::string> candidates;
            G2PApi::Error error = G2PApi::Error::None;
            G2PApi::HitSource hitSource = G2PApi::HitSource::Unspecified;

            /// Whether the input ordering of the contract already determined the result for this
            /// word. This flag differs from an error: a word on which a step failed still reaches
            /// the fallback step, whereas a settled word is complete before any step runs.
            bool settled = false;

            /// Returns whether a producing step may still process this word. Each word receives
            /// its pronunciation from exactly one source, and therefore the producing steps may
            /// appear in any order.
            bool eligible() const {
                return !settled && mode == G2PApi::Mode::Convert && pronunciation.empty();
            }
        };

        /// Returns \a candidates with \a pronunciation as the first element and without further
        /// occurrences of \a pronunciation.
        std::vector<std::string> leadWith(const std::string &pronunciation,
                                          const std::vector<std::string> &candidates) {
            std::vector<std::string> result{pronunciation};
            for (const auto &candidate : candidates) {
                if (candidate != pronunciation) {
                    result.push_back(candidate);
                }
            }
            return result;
        }

        srt::Expected<bool> readBool(const srt::JsonValue &value, std::string_view what) {
            if (!value.isBool()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " must be a boolean");
            }
            return value.toBool();
        }

        srt::Expected<fs::path> readPath(const srt::JsonValue &value, const fs::path &base,
                                         std::string_view what) {
            if (!value.isString() || value.toString().empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " must be a non-empty string");
            }
            auto path = pathFromManifest(value.toString());
            if (path.is_relative()) {
                path = base / path;
            }
            return path.lexically_normal();
        }

        srt::Expected<std::shared_ptr<const ChainDictionary>> loadDictionary(const fs::path &path) {
            auto loaded = ResourceCache::instance().acquire<ChainDictionary>(
                path, DICT_KIND, VARIANT,
                std::function([](const fs::path &resolved)
                                  -> srt::Expected<std::shared_ptr<const ChainDictionary>> {
                    auto parsed = ChainDictionary::load(resolved);
                    if (!parsed) {
                        return parsed.takeError();
                    }
                    return std::make_shared<const ChainDictionary>(parsed.take());
                }));
            if (!loaded) {
                return loaded.takeError();
            }
            return loaded.take();
        }

        srt::Expected<Step> readVerifyStep(const srt::JsonValue &params, const fs::path &base) {
            Step step;
            step.kind = StepKind::Verify;
            std::vector<VerifyEntry> entries;
            for (const auto &[key, item] : params.toObject()) {
                if (key != "entries") {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown verify step parameter: " + key);
                }
                auto read = readVerifyEntries(item, "verify entries");
                if (!read) {
                    return read.takeError();
                }
                entries = read.take();
            }
            auto verifier = Verifier::create(entries, base);
            if (!verifier) {
                return verifier.takeError();
            }
            step.verifier = std::make_shared<const Verifier>(verifier.take());
            return step;
        }

        srt::Expected<Step> readDictStep(const srt::JsonValue &params, const fs::path &base) {
            Step step;
            step.kind = StepKind::Dict;
            fs::path file;
            for (const auto &[key, item] : params.toObject()) {
                if (key != "file") {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown dict step parameter: " + key);
                }
                auto path = readPath(item, base, "dict step file");
                if (!path) {
                    return path.takeError();
                }
                file = path.take();
            }
            if (file.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "the dict step needs a file");
            }
            auto dictionary = loadDictionary(file);
            if (!dictionary) {
                return dictionary.takeError();
            }
            step.dictionary = dictionary.take();
            return step;
        }

        srt::Expected<Step> readFormatStep(const srt::JsonValue &params) {
            Step step;
            step.kind = StepKind::Format;
            for (const auto &[key, item] : params.toObject()) {
                if (key == "operations") {
                    if (!item.isArray()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "format step operations must be an array");
                    }
                    for (const auto &operation : item.toArray()) {
                        if (!operation.isString() || operation.toString() != "lowercase") {
                            return srt::Error(srt::Error::InvalidFormat,
                                              "the only format step operation this Level "
                                              "defines is lowercase");
                        }
                        step.lowercase = true;
                    }
                } else if (key == "stripTrailingSpace") {
                    auto value = readBool(item, "format step stripTrailingSpace");
                    if (!value) {
                        return value.takeError();
                    }
                    step.stripTrailing = value.take();
                } else if (key == "addSpaceBetweenPhones") {
                    auto value = readBool(item, "format step addSpaceBetweenPhones");
                    if (!value) {
                        return value.takeError();
                    }
                    step.addSpaces = value.take();
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown format step parameter: " + key);
                }
            }
            return step;
        }

        srt::Expected<Step> readModelStep(const srt::JsonValue &params) {
            Step step;
            step.kind = StepKind::Model;
            for (const auto &[key, item] : params.toObject()) {
                if (key == "role") {
                    if (!item.isString() || item.toString().empty()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "model step role must be a non-empty string");
                    }
                    step.role = item.toString();
                } else if (key == "batchSize") {
                    if (!item.isInt() || item.toInt() < 1) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "model step batchSize must be a positive integer");
                    }
                    step.batchSize = static_cast<int>(item.toInt());
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown model step parameter: " + key);
                }
            }
            if (step.role.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "the model step needs a role");
            }
            return step;
        }

        srt::Expected<Step> readFallbackStep(const srt::JsonValue &params) {
            Step step;
            step.kind = StepKind::Fallback;
            for (const auto &[key, item] : params.toObject()) {
                if (key == "useOriginal") {
                    auto value = readBool(item, "fallback step useOriginal");
                    if (!value) {
                        return value.takeError();
                    }
                    step.useOriginal = value.take();
                } else if (key == "defaultPronunciation") {
                    if (!item.isString()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "fallback step defaultPronunciation must be a string");
                    }
                    step.defaultPronunciation = item.toString();
                } else {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "unknown fallback step parameter: " + key);
                }
            }
            return step;
        }

        srt::Expected<std::vector<Step>> readSteps(const srt::JsonValue &value,
                                                   const fs::path &base) {
            if (!value.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, "steps must be an array");
            }
            const auto &array = value.toArray();
            if (array.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "steps must not be empty");
            }
            if (array.size() > MAX_STEPS) {
                return srt::Error(srt::Error::InvalidFormat, "a chain may hold at most " +
                                                                 std::to_string(MAX_STEPS) +
                                                                 " steps");
            }
            static const srt::JsonValue emptyParams{srt::JsonObject{}};
            std::vector<Step> steps;
            for (std::size_t i = 0; i < array.size(); ++i) {
                const auto position = "step " + std::to_string(i);
                if (!array[i].isObject()) {
                    return srt::Error(srt::Error::InvalidFormat, position + " must be an object");
                }
                std::string kind;
                bool enabled = true;
                const srt::JsonValue *params = &emptyParams;
                for (const auto &[key, item] : array[i].toObject()) {
                    if (key == "step") {
                        if (!item.isString() || item.toString().empty()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              position + " step must be a non-empty string");
                        }
                        kind = item.toString();
                    } else if (key == "enabled") {
                        auto value = readBool(item, position + " enabled");
                        if (!value) {
                            return value.takeError();
                        }
                        enabled = value.take();
                    } else if (key == "params") {
                        if (!item.isObject()) {
                            return srt::Error(srt::Error::InvalidFormat,
                                              position + " params must be an object");
                        }
                        params = &item;
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          position + " has an unknown key: " + key);
                    }
                }
                if (kind.empty()) {
                    return srt::Error(srt::Error::InvalidFormat, position + " needs a step type");
                }
                // The type of a disabled step is still validated, so that enabling the step later
                // cannot fail the package because of an unknown type.
                srt::Expected<Step> read = srt::Error(srt::Error::InvalidFormat,
                                                      position + " has an unknown type: " + kind);
                if (kind == "verify") {
                    read = enabled ? readVerifyStep(*params, base) : Step{StepKind::Verify};
                } else if (kind == "dict") {
                    read = enabled ? readDictStep(*params, base) : Step{StepKind::Dict};
                } else if (kind == "format") {
                    read = readFormatStep(*params);
                } else if (kind == "model") {
                    read = readModelStep(*params);
                } else if (kind == "fallback") {
                    read = readFallbackStep(*params);
                }
                if (!read) {
                    return read.takeError();
                }
                if (!enabled) {
                    continue;
                }
                steps.push_back(read.take());
            }
            // Classification determines which words the producing steps process, so a verify
            // step must precede every producing step. A later verify step would reclassify a word
            // that already has a pronunciation, and marking that word as copy would discard the
            // pronunciation: a chain that appears to refine the result would silently discard a
            // dictionary hit. This order is therefore rejected instead of being given a meaning.
            bool produced = false;
            for (const auto &step : steps) {
                if (step.kind == StepKind::Verify && produced) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "a verify step follows a step that produces "
                                      "pronunciations; classification determines which words "
                                      "those steps process, so it must precede them");
                }
                if (step.kind == StepKind::Dict || step.kind == StepKind::Model ||
                    step.kind == StepKind::Fallback) {
                    produced = true;
                }
            }
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (steps[i].kind != StepKind::Model) {
                    continue;
                }
                for (std::size_t j = i + 1; j < steps.size(); ++j) {
                    if (steps[j].kind == StepKind::Model && steps[j].role == steps[i].role) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "two model steps name the same role: " + steps[i].role);
                    }
                }
            }
            return steps;
        }

        /// Checks that every role a model step names is bound to a G2P module.
        ///
        /// The check runs after the bindings are created and before Commit, which is the only
        /// stage at which the contract of the target is known and a rejection still fails the
        /// load.
        class ImportValidator : public srt::ContribImportValidator {
        public:
            srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
                // Every registered validator receives every contribution in the transaction, so
                // the interface, level and variant must match before the configuration is cast to
                // the type of this plugin.
                if (spec.interface() != G2PApi::API_INTERFACE ||
                    spec.level() != G2PApi::API_LEVEL || spec.variant() != VARIANT) {
                    return {};
                }
                const auto *configuration =
                    static_cast<const Configuration *>(spec.configuration());
                if (configuration == nullptr) {
                    return {};
                }
                for (const auto &step : configuration->steps) {
                    if (step.kind != StepKind::Model) {
                        continue;
                    }
                    const auto import = spec.findImport(step.role);
                    if (!import) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "a model step names the role " + step.role +
                                              ", which this module does not import");
                    }
                    if (!import->binding()) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "the import for role " + step.role + " is not bound");
                    }
                    const auto &target = import->binding()->target();
                    if (target.interface() != G2PApi::API_INTERFACE ||
                        target.level() != G2PApi::API_LEVEL) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "the model step role " + step.role +
                                              " is bound to " + target.interface() +
                                              ", which is not the G2P contract");
                    }
                }
                return {};
            }
        };

        class Executive
            : public ExecutiveBase<G2PApi::G2PExecutive, G2PApi::G2PStartInput, G2PApi::G2PResult> {
        public:
            Executive(srt::InferenceSpec &spec, const Configuration *configuration,
                      Api::Common::L1::LanguageScheme binding)
                : ExecutiveBase(spec), m_configuration(configuration),
                  m_binding(std::move(binding)) {
            }

            ~Executive() {
                finish();
            }

            srt::Expected<void> initialize(const G2PApi::G2PInitArgs &) override {
                return {};
            }

            // quit() and wait() are deliberately not overridden: srt::InferenceExecutive already
            // forwards them to stop() and waitForFinished(), which is the path by which a package
            // unload stops a running conversion. Overriding them to return {} would silently
            // disable that path.

        protected:
            Batch runBatch(const G2PApi::G2PStartInput &input) override {
                // A stop request pending on entry cancels this batch. A stop request is consumed
                // only after a run, so a request that arrives between the check in the base class
                // and this call is honored rather than cleared. A request left over from an
                // earlier conversion cancels a batch that the base class did not intend to cancel;
                // the base class detects this case and retries the batch.
                std::vector<ChainWord> words;
                words.reserve(input.lyrics.size());
                for (const auto &lyric : input.lyrics) {
                    ChainWord word{lyric, lyric};
                    // The input ordering of the contract applies before the chain, so no step
                    // processes a word that the contract already settled.
                    switch (classifyLyric(lyric)) {
                        case LyricVerdict::Skip:
                            word.mode = G2PApi::Mode::Skip;
                            word.settled = true;
                            break;
                        case LyricVerdict::Invalid:
                            word.error = G2PApi::Error::InvalidInput;
                            word.settled = true;
                            break;
                        case LyricVerdict::Accept:
                            break;
                    }
                    words.push_back(std::move(word));
                }

                for (const auto &step : m_configuration->steps) {
                    // Stop requests are checked between steps rather than between words, because
                    // a step is the unit that either runs completely or does not run, and a
                    // partial dictionary pass over a batch is neither.
                    if (stopRequested()) {
                        return collect(words);
                    }
                    switch (step.kind) {
                        case StepKind::Verify:
                            runVerify(step, words);
                            break;
                        case StepKind::Dict:
                            runDict(step, words);
                            break;
                        case StepKind::Format:
                            runFormat(step, words);
                            break;
                        case StepKind::Model:
                            runModel(step, words);
                            break;
                        case StepKind::Fallback:
                            runFallback(step, words);
                            break;
                    }
                }

                return collect(words);
            }

        private:
            /// Converts the working words into contract output.
            ///
            /// Also called if a conversion stops partway, so that the caller receives the partial
            /// result of the chain rather than an error.
            static std::unique_ptr<G2PApi::G2PResult> collect(std::vector<ChainWord> &words) {
                auto result = std::make_unique<G2PApi::G2PResult>();
                result->words.reserve(words.size());
                for (auto &word : words) {
                    G2PApi::G2PWordOutput output;
                    output.mode = word.mode;
                    output.error = word.error;
                    output.hitSource = word.hitSource;
                    if (word.mode == G2PApi::Mode::Skip) {
                        // A skipped word has no pronunciation and no candidates.
                    } else if (word.error == G2PApi::Error::InvalidInput) {
                        // The lyric is returned unchanged so that a host can display the
                        // submitted text, and the error indicates that the module did not
                        // convert it.
                        output.pronunciation = word.lyric;
                    } else if (word.mode == G2PApi::Mode::Copy) {
                        // The pronunciation of a copy word is its lyric, and a copy word has no
                        // candidates.
                        output.pronunciation = word.lyric;
                    } else if (word.pronunciation.empty() && word.error == G2PApi::Error::None) {
                        // No step produced a pronunciation for this word and no fallback step
                        // covered it. Reporting success with an empty pronunciation would violate
                        // the contract rule that the pronunciation of a converted word is its
                        // first candidate, so the chain reports the failure. In a chain without a
                        // fallback step, every word that the dictionaries do not contain reaches
                        // this branch.
                        output.error = G2PApi::Error::PhonemeGenerationFailed;
                    } else if (word.error == G2PApi::Error::None) {
                        output.pronunciation = std::move(word.pronunciation);
                        output.candidates = std::move(word.candidates);
                    }
                    result->words.push_back(std::move(output));
                }
                return result;
            }

            static void runVerify(const Step &step, std::vector<ChainWord> &words) {
                if (!step.verifier) {
                    return;
                }
                std::vector<std::string> texts;
                texts.reserve(words.size());
                for (const auto &word : words) {
                    texts.push_back(word.text);
                }
                const auto modes = step.verifier->classify(texts);
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (words[i].settled) {
                        continue;
                    }
                    words[i].mode = modes[i] == VerifyMode::Convert ? G2PApi::Mode::Convert
                                                                    : G2PApi::Mode::Copy;
                }
            }

            static void runDict(const Step &step, std::vector<ChainWord> &words) {
                if (!step.dictionary) {
                    return;
                }
                for (auto &word : words) {
                    if (!word.eligible()) {
                        continue;
                    }
                    const auto *group = step.dictionary->find(word.text);
                    if (group == nullptr || group->empty()) {
                        continue;
                    }
                    word.pronunciation = group->front();
                    word.candidates = *group;
                    word.hitSource = G2PApi::HitSource::Dict;
                }
            }

            static void runFormat(const Step &step, std::vector<ChainWord> &words) {
                for (auto &word : words) {
                    if (step.lowercase && word.eligible()) {
                        word.text = toLowercase(word.text);
                    }
                    if (word.pronunciation.empty()) {
                        continue;
                    }
                    // The pronunciation is also the first candidate, so a rewrite applied only to
                    // the pronunciation would make the two values inconsistent.
                    word.pronunciation = tidy(step, word.pronunciation);
                    for (auto &candidate : word.candidates) {
                        candidate = tidy(step, candidate);
                    }
                }
            }

            static std::string tidy(const Step &step, const std::string &text) {
                auto result = text;
                if (step.stripTrailing) {
                    result = stripTrailingSpace(result);
                }
                if (step.addSpaces) {
                    result = addSpaceBetweenPhones(result);
                }
                return result;
            }

            void runModel(const Step &step, std::vector<ChainWord> &words) {
                auto resolved = resolveBackend(step.role);
                if (!resolved) {
                    for (auto &word : words) {
                        if (word.eligible()) {
                            word.error = G2PApi::Error::ModelInferenceFailed;
                        }
                    }
                    return;
                }
                auto *backend = resolved.take();

                // Eligible words are sent in contiguous runs rather than as one flat list, because
                // a backend may use neighbouring words as context, and words that are no longer
                // eligible must not act as neighbours.
                std::vector<std::size_t> run;
                const auto flush = [&] {
                    if (!run.empty()) {
                        convertRun(backend, run, words);
                        run.clear();
                    }
                };
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (words[i].eligible()) {
                        run.push_back(i);
                        if (step.batchSize > 0 &&
                            run.size() == static_cast<std::size_t>(step.batchSize)) {
                            flush();
                        }
                        continue;
                    }
                    flush();
                }
                flush();
            }

            static void convertRun(G2PApi::G2PExecutive *backend,
                                   const std::vector<std::size_t> &run,
                                   std::vector<ChainWord> &words) {
                G2PApi::G2PStartInput request;
                request.lyrics.reserve(run.size());
                for (const auto index : run) {
                    request.lyrics.push_back(words[index].text);
                }
                auto response = backend->start(request);
                if (!response) {
                    for (const auto index : run) {
                        words[index].error = G2PApi::Error::ModelInferenceFailed;
                    }
                    return;
                }
                const auto result = response.take();
                if (result->words.size() != run.size()) {
                    for (const auto index : run) {
                        words[index].error = G2PApi::Error::ModelInferenceFailed;
                    }
                    return;
                }
                for (std::size_t i = 0; i < run.size(); ++i) {
                    auto &word = words[run[i]];
                    const auto &produced = result->words[i];
                    // Word-level errors of the backend are passed through unchanged; the chain
                    // defines no error codes of its own.
                    word.error = produced.error;
                    if (produced.error != G2PApi::Error::None || produced.pronunciation.empty()) {
                        continue;
                    }
                    word.pronunciation = produced.pronunciation;
                    // The contract requires the pronunciation of a converted word to be its first
                    // candidate. The chain returns this word as its own output, so the chain
                    // enforces that invariant here instead of relying on every backend.
                    word.candidates = leadWith(produced.pronunciation, produced.candidates);
                    word.hitSource = G2PApi::HitSource::Model;
                }
            }

            static void runFallback(const Step &step, std::vector<ChainWord> &words) {
                for (auto &word : words) {
                    if (!word.eligible()) {
                        continue;
                    }
                    const auto filler = step.useOriginal ? word.lyric : step.defaultPronunciation;
                    if (filler.empty()) {
                        // No fallback value is available. The word keeps an empty pronunciation
                        // and reports a failure, rather than silently returning its text as a
                        // pronunciation.
                        word.error = G2PApi::Error::PhonemeGenerationFailed;
                        word.candidates.clear();
                        continue;
                    }
                    word.pronunciation = filler;
                    word.candidates = {filler};
                    word.hitSource = G2PApi::HitSource::Fallback;
                    // A word that the fallback step produced counts as a success, regardless of
                    // earlier failures.
                    word.error = G2PApi::Error::None;
                }
            }

            srt::Expected<G2PApi::G2PExecutive *> resolveBackend(const std::string &role) {
                const auto it = m_backends.find(role);
                if (it != m_backends.end()) {
                    return it->second;
                }
                const auto import = spec().findImport(role);
                if (!import || !import->binding()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the model step role " + role + " is not bound");
                }
                G2PApi::G2PRuntimeOptions options(import->binding()->target().variant());
                options.binding = m_binding;
                auto child = createChild(role, options);
                if (!child) {
                    return child.takeError();
                }
                auto *backend = static_cast<G2PApi::G2PExecutive *>(child.take());
                m_backends.emplace(role, backend);
                return backend;
            }

            const Configuration *m_configuration;
            Api::Common::L1::LanguageScheme m_binding;
            std::map<std::string, G2PApi::G2PExecutive *> m_backends;
        };

        class Interpreter : public srt::InferenceInterpreter {
        public:
            srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
                createImportValidators() const override {
                std::vector<std::unique_ptr<srt::ContribImportValidator>> validators;
                validators.push_back(std::make_unique<ImportValidator>());
                return validators;
            }

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
                                      "the chain configuration must be an object");
                }
                auto result = std::make_unique<Configuration>();
                bool hasFormatVersion = false;
                bool hasSteps = false;
                for (const auto &[key, item] : value.toObject()) {
                    if (key == "formatVersion") {
                        if (auto version = readFormatVersion(item, FORMAT_VERSION); !version) {
                            return version.takeError();
                        }
                        hasFormatVersion = true;
                    } else if (key == "steps") {
                        auto steps = readSteps(item, spec.declarationPath().parent_path());
                        if (!steps) {
                            return steps.takeError();
                        }
                        result->steps = steps.take();
                        hasSteps = true;
                    } else {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "unknown chain configuration key: " + key);
                    }
                }
                if (!hasFormatVersion) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the chain configuration needs a formatVersion");
                }
                if (!hasSteps) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "the chain configuration needs steps");
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
                return std::unique_ptr<srt::InferenceExecutive>(
                    new Executive(spec, configuration, binding));
            }
        };

    }

    class ChainPlugin final : public srt::InferenceInterpreterPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            if (interfaceName != G2PApi::API_INTERFACE || level != G2PApi::API_LEVEL ||
                variant != VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported chain contract or variant");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new Interpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::chain::ChainPlugin)
