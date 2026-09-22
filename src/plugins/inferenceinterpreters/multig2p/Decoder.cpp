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

        /// The tensor names the exported models use. They are part of the bundle's resource
        /// generation, not of any declaration, so they live beside the code that reads them.
        constexpr char SRC[] = "src";
        constexpr char LANG_IDS[] = "lang_ids";
        constexpr char SRC_PAD_MASK[] = "src_pad_mask";
        constexpr char ENCODER_OUT[] = "encoder_out";
        constexpr char DEC_INPUT[] = "dec_input";
        constexpr char LOGITS[] = "logits";

        /// The four cache tensors, named once as inputs and once as outputs.
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

        /// Runs one session and hands back the outputs it was asked for.
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

        /// Picks the highest scoring token of every row of a [batch, 1, vocabulary] logit tensor.
        srt::Expected<std::vector<std::int64_t>> argmax(const TensorPtr &logits,
                                                        std::size_t batch) {
            if (!logits || logits->dataType() != ds::ITensor::Float) {
                return srt::Error(srt::Error::InvalidFormat, "the logits are not floating point");
            }
            const auto values = logits->view<float>();
            if (values.empty() || values.size() % batch != 0) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the logits do not divide into one row per word");
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

        /// Splits a bundle language reference into the two parts a symbol prefix is built from.
        std::pair<std::string, std::string> splitRef(const std::string &ref) {
            const auto slash = ref.find('/');
            if (slash == std::string::npos) {
                return {ref, "default"};
            }
            return {ref.substr(0, slash), ref.substr(slash + 1)};
        }

        /// Turns one word into begin, its characters, end.
        ///
        /// A character the language has no symbol for becomes the unknown marker, which is what
        /// the models were trained to expect.
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

    srt::Expected<std::unique_ptr<Decoder>> Decoder::open(ds::InferenceDriver &driver,
                                                          const Bundle &bundle,
                                                          const std::filesystem::path &directory) {
        auto decoder = std::unique_ptr<Decoder>(new Decoder());
        const std::pair<const char *, std::unique_ptr<ds::InferenceSession> Decoder::*> models[] = {
            {"encoder", &Decoder::m_encoder},
            {"decoder_step_init", &Decoder::m_stepInit},
            {"decoder_step", &Decoder::m_step},
        };
        for (const auto &[logical, member] : models) {
            auto name = bundle.fileName(logical);
            if (!name) {
                return name.takeError();
            }
            const auto path = directory / pathFromManifest(name.take());
            if (!std::filesystem::is_regular_file(path)) {
                return srt::Error(srt::Error::FileNotFound,
                                  std::string("the bundle's ") + logical +
                                      " model is missing: " + stdc::path::to_utf8(path));
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
            decoder.get()->*member = std::move(session);
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

        // One padded block, because the models take the whole batch at once.
        std::vector<std::vector<std::int64_t>> encoded;
        encoded.reserve(batch);
        std::size_t width = 0;
        for (const auto &word : words) {
            encoded.push_back(encode(word, prefix, vocabulary));
            width = std::max(width, encoded.back().size());
        }

        std::vector<std::int64_t> source(batch * width, specials.padding);
        // The mask marks padding, so it starts true everywhere and is cleared over real symbols.
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
