#include "LinguistExecutiveImpl.h"

#include <string>
#include <utility>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Linguist/LinguistContrib.h>

namespace LinguistApi = wolf::Api::Linguist::L1;
namespace G2PApi = wolf::Api::G2P::L1;
namespace S2PApi = wolf::Api::S2P::L1;
namespace OnsetApi = wolf::Api::Onset::L1;

namespace wolf {

    namespace {

        /// Writes the language binding into runtime options that have a binding field.
        ///
        /// Overloads are used instead of a compile-time probe because whether a contract is
        /// language-bound is a property of the contract and is stated explicitly here. The Onset
        /// contract is not language-bound, so its runtime options have no binding field.
        void bind(G2PApi::G2PRuntimeOptions &options,
                  const Api::Common::L1::LanguageScheme &binding) {
            options.binding = binding;
        }

        void bind(S2PApi::S2PRuntimeOptions &options,
                  const Api::Common::L1::LanguageScheme &binding) {
            options.binding = binding;
        }

        void bind(OnsetApi::OnsetRuntimeOptions &, const Api::Common::L1::LanguageScheme &) {
        }

        bool lockedShapeIsValid(const LinguistApi::LockedPhonemes &locked) {
            return locked.phonemes.size() == locked.onsets.size();
        }

    }

    namespace {

        /// Rejects a stage result whose entry count differs from the input count.
        ///
        /// Such a stage violates its contract, and every later entry would be assigned to the
        /// wrong word. The result is rejected rather than truncated. Truncation left the words
        /// after the cut with an empty pronunciation and no error, which is indistinguishable from
        /// a conversion to an empty result, and those words were then passed to the later stages.
        /// The chain variant rejects a short batch from its backend and the session rejects a
        /// short batch from this executive; this check applies the same rule at the third point at
        /// which the mismatch can occur. A stop request is the only exception: a canceled stage
        /// returns the entries it completed, and the caller expects a partial result in that case.
        ///
        /// \return Success if \a produced equals \a requested; otherwise an InvalidFormat error.
        srt::Expected<void> checkBatchSize(std::size_t produced, std::size_t requested,
                                           const char *stage) {
            if (produced == requested) {
                return {};
            }
            return srt::Error(srt::Error::InvalidFormat,
                              std::string("the ") + stage + " stage returned " +
                                  std::to_string(produced) + " entries for " +
                                  std::to_string(requested) + " inputs");
        }

    }

    // ExecutiveBase reports and consumes the stop flag in one step, exactly once after runBatch()
    // returns, so a reused executive does not inherit the stop request (A73).
    LinguistExecutiveImpl::LinguistExecutiveImpl(LinguistSpec &spec)
        : ExecutiveBase(spec), m_binding(spec.language(), spec.scheme()) {
        if (const auto import = spec.findImport(LinguistApi::ROLE_G2P);
            import && import->binding()) {
            m_g2pContribution = import->binding()->target().locator();
        }
    }

    LinguistExecutiveImpl::~LinguistExecutiveImpl() {
        // Waits before any member that runBatch() reads is destroyed. The base class waits again
        // in its destructor, but the fields of this class are already destroyed at that point.
        finish();
    }

    const Api::Common::L1::LanguageScheme &LinguistExecutiveImpl::binding() const noexcept {
        return m_binding;
    }

    const srt::ContribLocator &LinguistExecutiveImpl::g2pContribution() const noexcept {
        return m_g2pContribution;
    }

    void LinguistExecutiveImpl::onStop() {
        // The conversion loop checks the flag between stages; the members are stopped here as
        // well, because a stage runs a whole batch in one call.
        if (auto g2p = m_g2p.load()) {
            (void) g2p->stop();
        }
        if (auto s2p = m_s2p.load()) {
            (void) s2p->stop();
        }
        if (auto onset = m_onset.load()) {
            (void) onset->stop();
        }
    }

    srt::Expected<void> LinguistExecutiveImpl::quit() {
        return stop();
    }

    srt::Expected<void> LinguistExecutiveImpl::wait() {
        return waitForFinished();
    }

    template <class Stage, class Input>
    auto LinguistExecutiveImpl::runStage(Stage &stage,
                                         const Input &input) -> decltype(stage.start(input)) {
        // The flag is checked after the stage has been created, and only then. A stop request
        // that arrives after the check by the caller but before the stage is published cannot be
        // forwarded to any stage, and a stage started afterwards would run its whole batch
        // without interruption. With this check, such a stop request is either detected here or,
        // if it arrives after the stage is published, forwarded to the stage, whose start consumes
        // it.
        if (stopRequested()) {
            return nullptr;
        }
        auto converted = stage.start(input);
        if (!converted) {
            return converted.takeError();
        }
        // A stage that reports Canceled although this conversion has no pending stop request was
        // canceled by a request left over from an earlier conversion, and its start has consumed
        // that request. The batch is therefore run again. A stop request for this conversion sets
        // the flag of this executive before it reaches the stage, so the check excludes that case.
        if (stage.state() == srt::ITask::Canceled && !stopRequested()) {
            converted = stage.start(input);
        }
        return converted;
    }

    template <class Options, class Executive>
    srt::Expected<Executive *> LinguistExecutiveImpl::createMember(std::string_view role) {
        const auto import = spec().findImport(role);
        if (!import || !import->binding()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist import " + std::string(role) + " is not bound");
        }
        Options options(import->binding()->target().variant());
        bind(options, m_binding);
        auto child = createChild(role, options);
        if (!child) {
            return child.takeError();
        }
        return static_cast<Executive *>(child.take());
    }

    srt::Expected<Api::G2P::L1::G2PExecutive *> LinguistExecutiveImpl::g2p() {
        if (!m_g2p.load()) {
            auto result = createMember<G2PApi::G2PRuntimeOptions, G2PApi::G2PExecutive>(
                LinguistApi::ROLE_G2P);
            if (!result) {
                return result.takeError();
            }
            m_g2p = result.take();
        }
        return m_g2p.load();
    }

    srt::Expected<Api::S2P::L1::S2PExecutive *> LinguistExecutiveImpl::s2p() {
        if (!m_s2p.load()) {
            // An absent S2P import is valid, as is an absent onset import; the composition then
            // ends at the pronunciation layer.
            if (!spec().findImport(LinguistApi::ROLE_S2P)) {
                return nullptr;
            }
            auto result = createMember<S2PApi::S2PRuntimeOptions, S2PApi::S2PExecutive>(
                LinguistApi::ROLE_S2P);
            if (!result) {
                return result.takeError();
            }
            m_s2p = result.take();
        }
        return m_s2p.load();
    }

    srt::Expected<Api::Onset::L1::OnsetExecutive *> LinguistExecutiveImpl::onset() {
        if (!m_onset.load()) {
            if (!spec().findImport(LinguistApi::ROLE_ONSET)) {
                return nullptr;
            }
            auto result = createMember<OnsetApi::OnsetRuntimeOptions, OnsetApi::OnsetExecutive>(
                LinguistApi::ROLE_ONSET);
            if (!result) {
                return result.takeError();
            }
            m_onset = result.take();
        }
        return m_onset.load();
    }

    LinguistExecutiveImpl::Batch
        LinguistExecutiveImpl::runBatch(const LinguistApi::LinguistConvertInput &input) {
        auto result = std::make_unique<LinguistApi::LinguistConvertResult>();
        result->words.resize(input.words.size());

        // A stop request that arrives before this call cancels the conversion. stop() applies to
        // the running conversion or, if none is running, to the next conversion. An earlier
        // version cleared the flag on entry instead, which silently discarded every stop request
        // issued before start(). That case is common: a host cancels, and then the queued batch
        // begins.
        if (stopRequested()) {
            return result;
        }

        // Pass one: determine the pronunciation layer of each word. Words that arrive with a pinned
        // pronunciation or a pinned phoneme layer skip the stages that would have produced them.
        G2PApi::G2PStartInput g2pInput;
        std::vector<std::size_t> g2pPositions;
        for (std::size_t i = 0; i < input.words.size(); ++i) {
            const auto &word = input.words[i];
            auto &output = result->words[i];

            if (word.locked) {
                if (!lockedShapeIsValid(*word.locked)) {
                    output.error = G2PApi::Error::InvalidInput;
                    continue;
                }
                output.mode = G2PApi::Mode::Copy;
                output.pronunciation = word.pronunciation.value_or(word.lyric);
                // A pinned layer is granted at the depth that asks for it, like the layer a stage
                // produces: phonemes from Phonemes upwards and onsets at Onsets. A caller that reads
                // an empty phoneme list as the mark of the depth it received is otherwise misled by
                // a single locked word of the batch.
                if (input.depth != LinguistApi::Depth::Pronunciation) {
                    output.phonemes = word.locked->phonemes;
                }
                if (input.depth == LinguistApi::Depth::Onsets) {
                    output.onsets = word.locked->onsets;
                }
                output.hitStage = LinguistApi::HitStage::Locked;
                continue;
            }
            if (word.pronunciation) {
                output.mode = G2PApi::Mode::Copy;
                output.pronunciation = *word.pronunciation;
                output.hitStage = LinguistApi::HitStage::Locked;
                continue;
            }
            g2pInput.lyrics.push_back(word.lyric);
            g2pPositions.push_back(i);
        }

        if (!g2pInput.lyrics.empty()) {
            auto executive = g2p();
            if (!executive) {
                return executive.takeError();
            }
            auto converted = runStage(**executive, g2pInput);
            if (!converted) {
                return converted.takeError();
            }
            if (*converted == nullptr) {
                return result;
            }
            auto words = converted.take();
            if (auto sized = checkBatchSize(words->words.size(), g2pPositions.size(), "g2p");
                !sized && !stopRequested()) {
                return sized.takeError();
            }
            for (std::size_t i = 0; i < g2pPositions.size() && i < words->words.size(); ++i) {
                auto &output = result->words[g2pPositions[i]];
                const auto &word = words->words[i];
                output.pronunciation = word.pronunciation;
                output.candidates = word.candidates;
                output.mode = word.mode;
                output.error = word.error;
                switch (word.hitSource) {
                    case G2PApi::HitSource::Dict:
                        output.hitStage = LinguistApi::HitStage::Dict;
                        break;
                    case G2PApi::HitSource::Model:
                        output.hitStage = LinguistApi::HitStage::Model;
                        break;
                    case G2PApi::HitSource::Rule:
                        output.hitStage = LinguistApi::HitStage::Rule;
                        break;
                    case G2PApi::HitSource::Fallback:
                        output.hitStage = LinguistApi::HitStage::Fallback;
                        break;
                    case G2PApi::HitSource::Unspecified:
                        break;
                }
            }
        }

        if (input.depth == LinguistApi::Depth::Pronunciation || stopRequested()) {
            // A stop returns the results completed so far rather than an error, because the
            // ExecutiveTask reports cancellation through state(), as ITask specifies.
            return result;
        }

        // Pass two: phonemes, for the words that do not already carry a pinned layer.
        S2PApi::S2PStartInput s2pInput;
        std::vector<std::size_t> s2pPositions;
        for (std::size_t i = 0; i < result->words.size(); ++i) {
            const auto &output = result->words[i];
            if (input.words[i].locked || output.error != G2PApi::Error::None ||
                output.mode == G2PApi::Mode::Skip) {
                continue;
            }
            s2pInput.pronunciations.push_back(output.pronunciation);
            s2pPositions.push_back(i);
        }

        auto phonemeStage = s2p();
        if (!phonemeStage) {
            return phonemeStage.takeError();
        }
        if (*phonemeStage == nullptr) {
            // Without an S2P member, the composition ends at the pronunciation layer. This result
            // is neither an error nor a silent truncation, because the host reads the limit from
            // LanguageStatus::maxDepth before it issues the request.
            return result;
        }

        if (!s2pInput.pronunciations.empty()) {
            auto converted = runStage(**phonemeStage, s2pInput);
            if (!converted) {
                return converted.takeError();
            }
            if (*converted == nullptr) {
                return result;
            }
            auto phonemes = converted.take();
            if (auto sized = checkBatchSize(phonemes->phonemes.size(), s2pPositions.size(), "s2p");
                !sized && !stopRequested()) {
                return sized.takeError();
            }
            for (std::size_t i = 0; i < s2pPositions.size() && i < phonemes->phonemes.size(); ++i) {
                result->words[s2pPositions[i]].phonemes = phonemes->phonemes[i];
            }
        }

        if (input.depth == LinguistApi::Depth::Phonemes || stopRequested()) {
            return result;
        }

        // Pass three: onsets. A composition without an onset member is not an error; every
        // position is marked as not an onset, which is the result that the contract specifies
        // for an uncovered position.
        auto marker = onset();
        if (!marker) {
            return marker.takeError();
        }

        OnsetApi::OnsetStartInput onsetInput;
        std::vector<std::size_t> onsetPositions;
        for (std::size_t i = 0; i < result->words.size(); ++i) {
            if (input.words[i].locked) {
                continue;
            }
            onsetInput.phonemes.push_back(result->words[i].phonemes);
            onsetPositions.push_back(i);
        }

        if (*marker && !onsetInput.phonemes.empty()) {
            auto marked = runStage(**marker, onsetInput);
            if (!marked) {
                return marked.takeError();
            }
            if (*marked == nullptr) {
                return result;
            }
            auto onsets = marked.take();
            if (auto sized = checkBatchSize(onsets->onsets.size(), onsetPositions.size(), "onset");
                !sized && !stopRequested()) {
                return sized.takeError();
            }
            for (std::size_t i = 0; i < onsetPositions.size() && i < onsets->onsets.size(); ++i) {
                result->words[onsetPositions[i]].onsets = onsets->onsets[i];
            }
        } else {
            for (const auto position : onsetPositions) {
                auto &output = result->words[position];
                output.onsets.assign(output.phonemes.size(), false);
            }
        }

        return result;
    }

}
