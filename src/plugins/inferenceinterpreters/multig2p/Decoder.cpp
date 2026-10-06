#include "Decoder.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Core/Tensor.h>

#include <stdcorelib/path.h>

#include <wolf/Support/ManifestValues.h>

namespace OnnxApi = ds::Api::Onnx;

namespace wolf::multig2p {

    namespace {

        /// Tensor names of the exported models. The names belong to the resource format version
        /// of the bundle rather than to any declaration, so they are defined beside the code that
        /// reads them.
        constexpr char SRC[] = "src";
        constexpr char LANG_IDS[] = "lang_ids";
        constexpr char SRC_PAD_MASK[] = "src_pad_mask";
        constexpr char ENCODER_OUT[] = "encoder_out";
        constexpr char DEC_INPUT[] = "dec_input";
        constexpr char LOGITS[] = "logits";

        /// Input and output names of the four cache tensors.
        struct CacheNames {
            const char *input;
            const char *output;
        };
        constexpr CacheNames CACHES[] = {
            {"kv_cache_k", "new_kv_cache_k"},
            {"kv_cache_v", "new_kv_cache_v"},
            {"kv_cache_cross_k", "new_kv_cache_cross_k"},
            {"kv_cache_cross_v", "new_kv_cache_cross_v"},
        };

        /// Logical names of the three models of a bundle, in the order that open() loads them. The
        /// names belong to the resource format version of the bundle rather than to any
        /// declaration, so they are defined beside the code that reads the models.
        constexpr const char *LOGICAL[] = {
            "encoder",
            "decoder_step_init",
            "decoder_step",
        };

        using TensorPtr = std::shared_ptr<ds::ITensor>;

        srt::Expected<TensorPtr> int64Tensor(const std::vector<std::int64_t> &shape,
                                             const std::vector<std::int64_t> &values) {
            auto tensor = ds::Tensor::createFromView<std::int64_t>(
                shape, stdc::array_view<std::int64_t>{values});
            if (!tensor) {
                return tensor.takeError();
            }
            return TensorPtr(tensor.take());
        }

        /// Runs \a session and returns its outputs, or an error if the run fails, returns no
        /// result, or omits an output listed in \a wanted.
        srt::Expected<std::map<std::string, TensorPtr>>
            invoke(ds::InferenceSession &session, std::map<std::string, TensorPtr> inputs,
                   const std::set<std::string> &wanted, const char *what) {
            OnnxApi::SessionStartInput request;
            request.inputs = std::move(inputs);
            request.outputs = wanted;

            auto response = session.start(request);
            if (!response) {
                return response.takeError().withContext(std::string("the ") + what +
                                                        " model failed");
            }
            auto result = response.take();
            const auto *outputs = result ? result->as<OnnxApi::SessionResult>() : nullptr;
            if (outputs == nullptr) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string("the ") + what + " model returned no outputs");
            }
            for (const auto &name : wanted) {
                const auto it = outputs->outputs.find(name);
                if (it == outputs->outputs.end() || !it->second) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      std::string("the ") + what + " model did not return " +
                                          name);
                }
            }
            return outputs->outputs;
        }

        /// Returns the index of the highest logit in every row of a [batch, 1, vocabulary] logit
        /// tensor, or an InvalidFormat error if the tensor is not floating point, is empty, or
        /// has a size that is not a multiple of \a batch.
        srt::Expected<std::vector<std::int64_t>> argmax(const TensorPtr &logits,
                                                        std::size_t batch) {
            if (!logits || logits->dataType() != ds::ITensor::Float) {
                return srt::Error(srt::Error::InvalidFormat, "the logits are not floating point");
            }
            const auto values = logits->view<float>();
            if (values.empty() || values.size() % batch != 0) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the logit tensor size is not a multiple of the batch size");
            }
            const auto width = values.size() / batch;
            std::vector<std::int64_t> tokens(batch);
            for (std::size_t i = 0; i < batch; ++i) {
                const auto *row = values.data() + i * width;
                tokens[i] = static_cast<std::int64_t>(
                    std::distance(row, std::max_element(row, row + width)));
            }
            return tokens;
        }

        /// Splits a bundle language reference into the language and the variant of a symbol
        /// prefix. A reference without a slash has the variant "default".
        std::pair<std::string, std::string> splitRef(const std::string &ref) {
            const auto slash = ref.find('/');
            if (slash == std::string::npos) {
                return {ref, "default"};
            }
            return {ref.substr(0, slash), ref.substr(slash + 1)};
        }

        /// Encodes one word as the begin marker, one id per byte of the word, and the end marker.
        ///
        /// A byte without a symbol, either prefixed with the language or unprefixed, is encoded as
        /// the unknown marker, which matches the encoding used in training.
        std::vector<std::int64_t> encode(const std::string &word, const std::string &prefix,
                                         const Vocabulary &vocabulary) {
            const auto &specials = vocabulary.specials();
            std::vector<std::int64_t> ids;
            ids.reserve(word.size() + 2);
            ids.push_back(specials.begin);
            for (const char character : word) {
                const std::string one(1, character);
                auto id = vocabulary.lookup(prefix + one);
                if (id < 0) {
                    id = vocabulary.lookup(one);
                }
                ids.push_back(id < 0 ? specials.unknown : id);
            }
            ids.push_back(specials.end);
            return ids;
        }

    }

    Decoder::Decoder() = default;

    Decoder::~Decoder() = default;

    srt::Expected<std::vector<ModelFile>>
        Decoder::resolveModels(const Bundle &bundle, const std::filesystem::path &directory) {
        std::vector<ModelFile> result;
        constexpr auto MODEL_COUNT = sizeof(LOGICAL) / sizeof(LOGICAL[0]);
        result.reserve(MODEL_COUNT);
        for (const auto *name : LOGICAL) {
            auto file = bundle.fileName(name);
            if (!file) {
                return file.takeError();
            }
            auto entry = ModelFile{name, directory / pathFromManifest(file.take())};
            if (!std::filesystem::is_regular_file(entry.path)) {
                return srt::Error(srt::Error::FileNotFound,
                                  std::string("the bundle's ") + name +
                                      " model is missing: " + stdc::path::to_utf8(entry.path));
            }
            result.push_back(std::move(entry));
        }
        return result;
    }

    srt::Expected<void> Decoder::verifyModels(const Bundle &bundle,
                                              const std::filesystem::path &directory) {
        if (auto models = resolveModels(bundle, directory); !models) {
            return models.takeError();
        }
        return {};
    }

    srt::Expected<std::unique_ptr<Decoder>> Decoder::open(ds::InferenceDriver &driver,
                                                          const Bundle &bundle,
                                                          const std::filesystem::path &directory) {
        auto models = resolveModels(bundle, directory);
        if (!models) {
            return models.takeError();
        }
        auto decoder = std::unique_ptr<Decoder>(new Decoder());
        const auto resolved = models.take();

        // A model is loaded into the slot that its logical name selects, rather than into the slot
        // at the same position. The two lists used to be aligned by position, where reordering
        // LOGICAL silently swapped two models: nothing in the compiler or in the tests compared a
        // name with the slot that received it. The guard below reports a name that has no slot,
        // which is what an edit that extends LOGICAL without extending this lookup hits.
        const auto slotFor = [&decoder](const std::string &logical) {
            if (logical == "encoder") {
                return &decoder->m_encoder;
            }
            if (logical == "decoder_step_init") {
                return &decoder->m_stepInit;
            }
            if (logical == "decoder_step") {
                return &decoder->m_step;
            }
            return static_cast<std::unique_ptr<ds::InferenceSession> *>(nullptr);
        };

        for (const auto &[logical, path] : resolved) {
            auto *slot = slotFor(logical);
            if (slot == nullptr) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "no session slot holds the model " + logical);
            }
            auto session = driver.createSession();
            if (!session) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  std::string("the driver created no session for ") + logical);
            }
            OnnxApi::SessionOpenArgs args;
            if (auto opened = session->open(path, args); !opened) {
                return opened.takeError().withContext(std::string("cannot open the ") + logical +
                                                      " model");
            }
            *slot = std::move(session);
        }
        return decoder;
    }

    srt::Expected<std::vector<std::vector<std::int64_t>>>
        Decoder::run(const std::vector<std::string> &words, const std::string &languageRef,
                     std::int64_t languageId, const Vocabulary &vocabulary, int maxLength,
                     const std::atomic_bool &stopped) const {
        const auto batch = words.size();
        if (batch == 0) {
            return std::vector<std::vector<std::int64_t>>();
        }
        const auto &specials = vocabulary.specials();
        const auto [language, variant] = splitRef(languageRef);
        const std::string prefix = language + "/" + variant + "/";

        // The words form one padded block, because the models process the whole batch at once.
        std::vector<std::vector<std::int64_t>> encoded;
        encoded.reserve(batch);
        std::size_t width = 0;
        for (const auto &word : words) {
            encoded.push_back(encode(word, prefix, vocabulary));
            width = std::max(width, encoded.back().size());
        }

        std::vector<std::int64_t> source(batch * width, specials.padding);
        // The mask marks padding, so every element starts as true and is cleared at positions
        // that hold symbols.
        ds::Tensor::Container maskBytes(batch * width, std::byte{1});
        for (std::size_t i = 0; i < batch; ++i) {
            for (std::size_t j = 0; j < encoded[i].size(); ++j) {
                source[i * width + j] = encoded[i][j];
                maskBytes[i * width + j] = std::byte{0};
            }
        }
        const std::vector<std::int64_t> blockShape{static_cast<std::int64_t>(batch),
                                                   static_cast<std::int64_t>(width)};
        auto sourceTensor = int64Tensor(blockShape, source);
        if (!sourceTensor) {
            return sourceTensor.takeError();
        }
        auto maskTensor =
            ds::Tensor::createFromRawData(ds::ITensor::Bool, blockShape, std::move(maskBytes));
        if (!maskTensor) {
            return maskTensor.takeError();
        }
        auto languageTensor =
            int64Tensor({static_cast<std::int64_t>(batch)},
                        std::vector<std::int64_t>(batch, languageId));
        if (!languageTensor) {
            return languageTensor.takeError();
        }

        const TensorPtr sourceValue = sourceTensor.take();
        const TensorPtr maskValue = maskTensor.take();
        const TensorPtr languageValue = languageTensor.take();

        auto encoderOut = invoke(*m_encoder,
                                 {{SRC, sourceValue},
                                  {LANG_IDS, languageValue},
                                  {SRC_PAD_MASK, maskValue}},
                                 {ENCODER_OUT}, "encoder");
        if (!encoderOut) {
            return encoderOut.takeError();
        }

        std::set<std::string> stepOutputs{LOGITS};
        for (const auto &cache : CACHES) {
            stepOutputs.insert(cache.output);
        }

        auto begin = int64Tensor({static_cast<std::int64_t>(batch), 1},
                                 std::vector<std::int64_t>(batch, specials.begin));
        if (!begin) {
            return begin.takeError();
        }
        auto initial = invoke(*m_stepInit,
                              {{DEC_INPUT, TensorPtr(begin.take())},
                               {LANG_IDS, languageValue},
                               {ENCODER_OUT, encoderOut->at(ENCODER_OUT)},
                               {SRC_PAD_MASK, maskValue}},
                              stepOutputs, "decoder_step_init");
        if (!initial) {
            return initial.takeError();
        }
        auto state = initial.take();

        auto tokens = argmax(state[LOGITS], batch);
        if (!tokens) {
            return tokens.takeError();
        }
        auto next = tokens.take();

        std::vector<std::vector<std::int64_t>> produced(batch);
        std::vector<bool> finished(batch, false);
        const auto record = [&] {
            for (std::size_t i = 0; i < batch; ++i) {
                if (finished[i]) {
                    continue;
                }
                if (next[i] == specials.end) {
                    finished[i] = true;
                } else {
                    produced[i].push_back(next[i]);
                }
            }
        };
        record();

        for (int step = 1; step < maxLength; ++step) {
            if (stopped) {
                break;
            }
            if (std::all_of(finished.begin(), finished.end(), [](bool done) { return done; })) {
                break;
            }
            std::vector<std::int64_t> feed(batch);
            for (std::size_t i = 0; i < batch; ++i) {
                feed[i] = finished[i] ? specials.padding : next[i];
            }
            auto input = int64Tensor({static_cast<std::int64_t>(batch), 1}, feed);
            if (!input) {
                return input.takeError();
            }
            std::map<std::string, TensorPtr> arguments{
                {DEC_INPUT, TensorPtr(input.take())},
                {LANG_IDS, languageValue},
                {SRC_PAD_MASK, maskValue},
            };
            for (const auto &cache : CACHES) {
                arguments.emplace(cache.input, state[cache.output]);
            }
            auto advanced = invoke(*m_step, std::move(arguments), stepOutputs, "decoder_step");
            if (!advanced) {
                return advanced.takeError();
            }
            state = advanced.take();

            auto stepped = argmax(state[LOGITS], batch);
            if (!stepped) {
                return stepped.takeError();
            }
            next = stepped.take();
            record();
        }
        return produced;
    }

}
