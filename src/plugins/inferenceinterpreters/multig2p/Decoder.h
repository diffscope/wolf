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

    /// One model of a bundle and the file it was resolved to.
    struct ModelFile {
        /// The logical model name recorded in the bundle ("encoder", "decoder_step_init",
        /// "decoder_step"). The name belongs to the resource format rather than to any declaration.
        std::string logical;

        /// The file of that model inside the bundle directory.
        std::filesystem::path path;
    };

    /// Runs one batch of words through the sequence-to-sequence models of a bundle.
    ///
    /// Decoding is greedy: each step emits the token with the highest logit. The sessions belong
    /// to this object because a session runs one execution at a time, for the same reason that an
    /// executive runs one conversion at a time.
    class Decoder {
    public:
        /// Resolves the three models that \a bundle names to files in \a directory, and reports a
        /// missing file as an error.
        ///
        /// This is the only place that turns a model of a bundle into a path, so that the load-time
        /// check of a configuration and the run-time \c open() cannot disagree about which files a
        /// bundle names or about how a missing one is reported. The load-time caller reaches this
        /// through \c verifyModels().
        ///
        /// \return one entry per model, in the order that \c open() loads them, or an error if the
        /// bundle records no file for a model or a file is missing.
        static srt::Expected<std::vector<ModelFile>>
            resolveModels(const Bundle &bundle, const std::filesystem::path &directory);

        /// Checks that every model of \a bundle can be resolved, without creating a session: the
        /// load of a package rejects a bundle whose models are missing, instead of accepting it and
        /// failing word by word at run time.
        ///
        /// The check is read-only and leaves nothing to roll back, which is the property that lets
        /// it run in Acquire (spec 2.4:446). Opening the models themselves stays in
        /// \c createInference(), because a session is a run-time resource, and because a missing
        /// inference driver is an installation error that the contract reports per word instead.
        ///
        /// \return nothing, or the error that \c resolveModels() reports.
        static srt::Expected<void> verifyModels(const Bundle &bundle,
                                                const std::filesystem::path &directory);

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
