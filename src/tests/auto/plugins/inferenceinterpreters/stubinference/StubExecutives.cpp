#include "StubExecutives.h"

#include <chrono>
#include <sstream>
#include <thread>

#include <utility>

#include <executiveutil/ExecutiveBase.h>
#include <linguistutil/ManifestValues.h>

namespace G2pApi = wolf::Api::G2p::L1;
namespace S2pApi = wolf::Api::S2p::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf::stub {

    namespace {

        /// The three stubs share the task implementation of the real variants, so that a stub
        /// substitutes accurately for a real variant: a stub with an incorrect waitForFinished()
        /// would hide exactly the class of defect that the real variants had. A stub has no work to
        /// cancel within a batch, so its runBatch() never reads the stop flag; a stop request still
        /// sets the run state to Canceled, as it does for a real variant. The one exception is the
        /// hold of StubConfiguration::holdMs, which is what gives a batch something to cancel.
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
        srt::Expected<std::vector<std::string>> readOptionalStringSet(const srt::ContribSpec &spec,
                                                                      std::string_view key) {
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
            explicit StubConfiguration(srt::ContribContractTriplet triplet)
                : ContribConfiguration(std::move(triplet)) {
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

            /// Milliseconds a batch of this stub holds before it answers, used to test that a stop
            /// request reaches the backends of a chain rather than waiting for the batch to end.
            /// Zero, which every fixture but that one sets, holds nothing.
            int holdMs = 0;
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

        class StubG2pExecutive
            : public StubExecutive<G2pApi::G2pExecutive, G2pApi::G2pStartInput, G2pApi::G2pResult> {
        public:
            StubG2pExecutive(srt::InferenceSpec &spec, std::size_t dropWords, int holdMs)
                : ExecutiveBase(spec), m_dropWords(dropWords), m_holdMs(holdMs) {
            }

            ~StubG2pExecutive() {
                finish();
            }

            srt::Expected<void> initialize(const G2pApi::G2pInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const G2pApi::G2pStartInput &input) override {
                // The one thing this stub waits for: the hold stands for the work of a model or a
                // script backend, and a stop request ends it, which is how a caller can tell
                // whether the request reached this executive at all.
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(m_holdMs);
                while (m_holdMs > 0 && std::chrono::steady_clock::now() < deadline &&
                       !stopRequested()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                auto result = std::make_unique<G2pApi::G2pResult>();
                result->words.reserve(input.lyrics.size());
                for (const auto &lyric : input.lyrics) {
                    G2pApi::G2pWordOutput word;
                    if (lyric.empty()) {
                        word.mode = G2pApi::Mode::Skip;
                    } else {
                        word.mode = G2pApi::Mode::Copy;
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
            int m_holdMs = 0;
        };

        class StubS2pExecutive
            : public StubExecutive<S2pApi::S2pExecutive, S2pApi::S2pStartInput, S2pApi::S2pResult> {
        public:
            explicit StubS2pExecutive(srt::InferenceSpec &spec) : ExecutiveBase(spec) {
            }

            srt::Expected<void> initialize(const S2pApi::S2pInitArgs &) override {
                return {};
            }

        protected:
            Batch runBatch(const S2pApi::S2pStartInput &input) override {
                auto result = std::make_unique<S2pApi::S2pResult>();
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
        StubG2pInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto languages = readOptionalLanguages(spec);
        if (!languages) {
            return languages.takeError();
        }
        auto symbols = readOptionalStringSet(spec, "symbols");
        if (!symbols) {
            return symbols.takeError();
        }
        auto result = std::make_unique<G2pApi::G2pExports>(m_variant);
        result->languages = languages.take();
        result->symbols = symbols.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubG2pInterpreter::createConfiguration(const srt::ContribSpec &spec) const {
        auto result = std::make_unique<StubConfiguration>(
            srt::ContribContractTriplet(G2pApi::API_INTERFACE, G2pApi::API_LEVEL, m_variant));
        if (spec.manifestConfiguration().isObject()) {
            const auto &object = spec.manifestConfiguration().toObject();
            if (const auto it = object.find("dropWords"); it != object.end()) {
                if (!it->second.isInt() || it->second.toInt() < 0) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "stub dropWords must be a non-negative integer");
                }
                result->dropWords = static_cast<std::size_t>(it->second.toInt());
            }
            if (const auto it = object.find("holdMs"); it != object.end()) {
                if (!it->second.isInt() || it->second.toInt() < 0) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "stub holdMs must be a non-negative integer");
                }
                result->holdMs = static_cast<int>(it->second.toInt());
            }
        }
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        StubG2pInterpreter::createImportOptions(const srt::ContribSpec &,
                                                const srt::JsonValue &) const {
        return std::unique_ptr<srt::ContribImportOptions>(new G2pApi::G2pImportOptions(m_variant));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        StubG2pInterpreter::createInference(srt::InferenceSpec &spec,
                                            const srt::ContribImportOptions &,
                                            const srt::InferenceRuntimeOptions &) {
        const auto *configuration = static_cast<const StubConfiguration *>(spec.configuration());
        return std::unique_ptr<srt::InferenceExecutive>(
            new StubG2pExecutive(spec, configuration ? configuration->dropWords : 0,
                                 configuration ? configuration->holdMs : 0));
    }

    // ---------------------------------------------------------------- S2P

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        StubS2pInterpreter::createExports(const srt::ContribSpec &spec) const {
        auto languages = readOptionalLanguages(spec);
        if (!languages) {
            return languages.takeError();
        }
        auto phonemes = readOptionalStringSet(spec, "phonemes");
        if (!phonemes) {
            return phonemes.takeError();
        }
        auto result = std::make_unique<S2pApi::S2pExports>(m_variant);
        result->languages = languages.take();
        result->phonemes = phonemes.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        StubS2pInterpreter::createConfiguration(const srt::ContribSpec &) const {
        return std::unique_ptr<srt::ContribConfiguration>(
            new StubConfiguration({S2pApi::API_INTERFACE, S2pApi::API_LEVEL, m_variant}));
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        StubS2pInterpreter::createImportOptions(const srt::ContribSpec &,
                                                const srt::JsonValue &) const {
        return std::unique_ptr<srt::ContribImportOptions>(new S2pApi::S2pImportOptions(m_variant));
    }

    srt::Expected<std::unique_ptr<srt::InferenceExecutive>>
        StubS2pInterpreter::createInference(srt::InferenceSpec &spec,
                                            const srt::ContribImportOptions &,
                                            const srt::InferenceRuntimeOptions &) {
        return std::unique_ptr<srt::InferenceExecutive>(new StubS2pExecutive(spec));
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
            new StubConfiguration({OnsetApi::API_INTERFACE, OnsetApi::API_LEVEL, m_variant}));
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
