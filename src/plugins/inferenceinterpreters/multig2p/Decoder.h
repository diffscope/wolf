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

    /// Runs one batch of words through the sequence-to-sequence models of a bundle.
    ///
    /// Decoding is greedy: each step emits the token with the highest logit. The sessions belong
    /// to this object because a session runs one execution at a time, for the same reason that an
    /// executive runs one conversion at a time.
    class Decoder {
    public:
        /// Opens the three models that \a bundle names, located in \a directory.
        ///
        /// \return the decoder, or an error if the bundle records no file for a model, a model
        /// file is missing, the driver creates no session, or a model fails to open.
        static srt::Expected<std::unique_ptr<Decoder>>
            open(ds::InferenceDriver &driver, const Bundle &bundle,
                 const std::filesystem::path &directory);

        ~Decoder();

        /// Converts \a words, which all belong to one language, into token id sequences.
        ///
        /// \return one sequence per word, in input order, without the begin and end markers; the
        /// sequences decoded so far if \a stopped is set; or an error if a tensor cannot be
        /// created or a model run fails.
        ///
        /// \a stopped is checked between decode steps, which are the only boundaries in this
        /// loop: the whole batch advances one token at a time, so no per-word stopping point
        /// exists.
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
