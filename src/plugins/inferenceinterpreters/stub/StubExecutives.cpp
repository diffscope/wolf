#include "StubExecutives.h"

#include <sstream>

#include <utility>

#include <wolf/Support/ExecutiveBase.h>
#include <wolf/Support/ManifestValues.h>

namespace G2PApi = wolf::Api::G2P::L1;
namespace S2PApi = wolf::Api::S2P::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::stub {

    namespace {

        /// The three stubs share the task implementation of the real variants, so that a stub
        /// substitutes accurately for a real variant: a stub with an incorrect waitForFinished()
        /// would hide exactly the class of defect that the real variants had. A stub has no work to
        /// cancel within a batch, so its runBatch() never reads the stop flag; a stop request still
        /// sets the run state to Canceled, as it does for a real variant.
        template <class Base, class Input, class Result>
        using StubExecutive = ExecutiveBase<Base, Input, Result>;

        /// Reads the optional exports.languages key shared by G2P and S2P.
        ///
        /// \return The declared pairs; an empty list if the exports are not an object or have no
        /// languages key; an error if the value is malformed.
        srt::Expected<std::vector<Api::Common::L1::LanguageScheme>>
            readOptionalLanguages(const srt::ContribSpec &spec) {
            if (!spec.manifestExports().isObject()) {
                return std::vector<Api::Common::L1::LanguageScheme>();
            }
            const auto &object = spec.manifestExports().toObject();
            const auto it = object.find("languages");
            if (it == object.end()) {
                return std::vector<Api::Common::L1::LanguageScheme>();
            }
            return readLanguageSchemes(it->second, "exports languages");
        }

        /// Reads one optional string set from the exports.
        ///
        /// \return The strings; an empty list if the exports are not an object or have no \a key;
        /// an error if the value is malformed.
        srt::Expected<std::vector<std::string>>
            readOptionalStringSet(const srt::ContribSpec &spec, std::string_view key) {
            if (!spec.manifestExports().isObject()) {
                return std::vector<std::string>();
            }
            const auto &object = spec.manifestExports().toObject();
            const auto it = object.find(key);
            if (it == object.end()) {
                return std::vector<std::string>();
            }
            return readStringSet(it->second, spec.declarationPath().parent_path(), key);
        }

        /// Each variant defines its own configuration type, because the configuration block is
        /// owned by the variant rather than by the contract. The contract headers therefore
        /// declare no reusable configuration class.
        class StubConfiguration : public srt::ContribConfiguration {
        public:
            StubConfiguration(std::string interfaceName, std::string variant, int level)
                : ContribConfiguration(std::move(interfaceName), std::move(variant), level) {
            }

            /// Number of words to omit from the result, used to test callers against a module that
            /// returns the wrong number of entries.
            ///
            /// A returned batch shorter than its input violates the provider ABI, and three layers
            /// reject it: the chain variant for its backend, the linguist executive for its
            /// stages, and the session for the executive. No shipped module produces such a batch,
            /// so the stub must produce it; a guard against a contract violation has value only if
            /// a test demonstrates that the guard triggers.
            std::size_t dropWords = 0;
        };

        std::vector<std::string> splitOnSpaces(const std::string &text) {
            std::vector<std::string> result;
            std::istringstream stream(text);
            std::string token;
            while (stream >> token) {
                result.push_back(token);
            }
            return result;
        }

        class StubG2PExecutive
            : public StubExecutive<G2PApi::G2PExecutive, G2PApi::G2PStartInput, G2PApi::G2PResult> {
        public:
            StubG2PExecutive(srt::InferenceSpec &spec, std::size_t dropWords)
                : ExecutiveBase(spec), m_dropWords(dropWords) {
            }

            ~StubG2PExecutive() {
                finish();
            }

            srt::Expected<void> initialize(const G2PApi::G2PInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const G2PApi::G2PStartInput &input) override {
                auto result = std::make_unique<G2PApi::G2PResult>();
                result->words.reserve(input.lyrics.size());
                for (const auto &lyric : input.lyrics) {
                    G2PApi::G2PWordOutput word;
                    if (lyric.empty()) {
                        word.mode = G2PApi::Mode::Skip;
                    } else {
                        word.mode = G2PApi::Mode::Copy;
                        word.pronunciation = lyric;
                    }
                    result->words.push_back(std::move(word));
                }
                // Nonzero only for a fixture that sets dropWords explicitly.
                if (m_dropWords > 0) {
                    result->words.resize(m_dropWords >= result->words.size()
                                             ? 0
                                             : result->words.size() - m_dropWords);
                }
                return result;
            }

        private:
            std::size_t m_dropWords = 0;
        };

        class StubS2PExecutive
            : public StubExecutive<S2PApi::S2PExecutive, S2PApi::S2PStartInput, S2PApi::S2PResult> {
        public:
            explicit StubS2PExecutive(srt::InferenceSpec &spec) : ExecutiveBase(spec) {
            }

            srt::Expected<void> initialize(const S2PApi::S2PInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const S2PApi::S2PStartInput &input) override {
                auto result = std::make_unique<S2PApi::S2PResult>();
                result->phonemes.reserve(input.pronunciations.size());
                for (const auto &pronunciation : input.pronunciations) {
                    result->phonemes.push_back(splitOnSpaces(pronunciation));
                }
                return result;
            }
        };

        class StubOnsetExecutive
            : public StubExecutive<OnsetApi::OnsetExecutive, OnsetApi::OnsetStartInput,
                                   OnsetApi::OnsetResult> {
        public:
            explicit StubOnsetExecutive(srt::InferenceSpec &spec) : ExecutiveBase(spec) {
            }

            srt::Expected<void> initialize(const OnsetApi::OnsetInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const OnsetApi::OnsetStartInput &input) override {
                auto result = std::make_unique<OnsetApi::OnsetResult>();
                result->onsets.reserve(input.phonemes.size());
                for (const auto &sequence : input.phonemes) {
                    result->onsets.emplace_back(sequence.size(), false);
                }
                return result;
            }
        };

    }

    // ---------------------------------------------------------------- G2P

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        StubG2PInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto languages = readOptionalLanguages(spec);
        if (!languages) {
            return languages.takeError();
        }
        auto symbols = readOptionalStringSet(spec, "symbols");
        if (!symbols) {
            return symbols.takeError();
        }
        auto result = std::make_unique<G2PApi::G2PExports>(m_variant);
        result->languages = languages.take();
        result->symbols = symbols.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubG2PInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        auto result = std::make_unique<StubConfiguration>(G2PApi::API_INTERFACE, m_variant,
                                                          G2PApi::API_LEVEL);
        if (spec.manifestConfiguration().isObject()) {
            const auto &object = spec.manifestConfiguration().toObject();
            if (const auto it = object.find("dropWords"); it != object.end()) {
                if (!it->second.isInt() || it->second.toInt() < 0) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "stub dropWords must be a non-negative integer");
                }
                result->dropWords = static_cast<std::size_t>(it->second.toInt());
            }
        }
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        StubG2PInterpreter::createImportOptions(const srt::ContribSpec &,
                                                const srt::JsonValue &) const {
        return std::unique_ptr<srt::ContribImportOptions>(new G2PApi::G2PImportOptions(m_variant));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        StubG2PInterpreter::createInference(srt::InferenceSpec &spec,
                                            const srt::ContribImportOptions &,
                                            const srt::InferenceRuntimeOptions &) {
        const auto *configuration = static_cast<const StubConfiguration *>(spec.configuration());
        return std::unique_ptr<srt::InferenceExecutive>(
            new StubG2PExecutive(spec, configuration ? configuration->dropWords : 0));
    }

    // ---------------------------------------------------------------- S2P

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        StubS2PInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto languages = readOptionalLanguages(spec);
        if (!languages) {
            return languages.takeError();
        }
        auto phonemes = readOptionalStringSet(spec, "phonemes");
        if (!phonemes) {
            return phonemes.takeError();
        }
        auto result = std::make_unique<S2PApi::S2PExports>(m_variant);
        result->languages = languages.take();
        result->phonemes = phonemes.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubS2PInterpreter::createConfiguration(const srt::ContribSpec &) const {
        return std::unique_ptr<srt::ContribConfiguration>(
            new StubConfiguration(S2PApi::API_INTERFACE, m_variant, S2PApi::API_LEVEL));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        StubS2PInterpreter::createImportOptions(const srt::ContribSpec &,
                                                const srt::JsonValue &) const {
        return std::unique_ptr<srt::ContribImportOptions>(new S2PApi::S2PImportOptions(m_variant));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        StubS2PInterpreter::createInference(srt::InferenceSpec &spec,
                                            const srt::ContribImportOptions &,
                                            const srt::InferenceRuntimeOptions &) {
        return std::unique_ptr<srt::InferenceExecutive>(new StubS2PExecutive(spec));
    }

    // ---------------------------------------------------------------- Onset

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        StubOnsetInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto known = readOptionalStringSet(spec, "knownPhonemes");
        if (!known) {
            return known.takeError();
        }
        auto result = std::make_unique<OnsetApi::OnsetExports>(m_variant);
        result->knownPhonemes = known.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubOnsetInterpreter::createConfiguration(const srt::ContribSpec &) const {
        return std::unique_ptr<srt::ContribConfiguration>(
            new StubConfiguration(OnsetApi::API_INTERFACE, m_variant, OnsetApi::API_LEVEL));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        StubOnsetInterpreter::createImportOptions(const srt::ContribSpec &,
                                                  const srt::JsonValue &) const {
        return std::unique_ptr<srt::ContribImportOptions>(
            new OnsetApi::OnsetImportOptions(m_variant));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        StubOnsetInterpreter::createInference(srt::InferenceSpec &spec,
                                              const srt::ContribImportOptions &,
                                              const srt::InferenceRuntimeOptions &) {
        return std::unique_ptr<srt::InferenceExecutive>(new StubOnsetExecutive(spec));
    }

}
