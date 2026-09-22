#ifndef WOLF_MULTIG2P_DECODER_H
#define WOLF_MULTIG2P_DECODER_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>

#include <synthrt/Support/Expected.h>

#include "Bundle.h"

namespace wolf::multig2p {

    /// Runs one batch of words through the sequence to sequence models of a bundle.
    ///
    /// Decoding is greedy: one token per step, taken from the highest scoring logit. The sessions
    /// belong to this object because a session carries one execution at a time, which is the same
    /// reason an executive carries one conversion.
    class Decoder {
    public:
        /// Opens the three models a bundle names, found under \a directory.
        static srt::Expected<std::unique_ptr<Decoder>>
            open(ds::InferenceDriver &driver, const Bundle &bundle,
                 const std::filesystem::path &directory);

        ~Decoder();

        /// Converts \a words, all bound to one language, into token id sequences.
        ///
        /// Returns one sequence per word, in order, with the begin and end markers removed.
        ///
        /// \a stopped is read between decode steps, which is the only boundary this loop has:
        /// the whole batch advances one token at a time, so there is no per word point to stop at.
        srt::Expected<std::vector<std::vector<std::int64_t>>>
            run(const std::vector<std::string> &words, const std::string &languageRef,
                std::int64_t languageId, const Vocabulary &vocabulary, int maxLength,
                const std::atomic_bool &stopped) const;

    private:
        Decoder();

        std::unique_ptr<ds::InferenceSession> m_encoder;
        std::unique_ptr<ds::InferenceSession> m_stepInit;
        std::unique_ptr<ds::InferenceSession> m_step;
    };

}

#endif // WOLF_MULTIG2P_DECODER_H
