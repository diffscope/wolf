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

        /// Writes the language binding into the runtime options that have one.
        ///
        /// Overloads rather than a compile-time probe: which contracts are language bound is a
        /// contract fact worth stating, and Onset genuinely is not one of them, so it carries no
        /// binding field to leave unused.
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

        /// A stage that returns a different number of entries than it was given has broken its
        /// contract, and every later entry would land on the wrong word.
        ///
        /// Refused rather than truncated. Truncating left the words past the cut carrying an empty
        /// pronunciation and no error, which reads as "converted to nothing" — and those words then
        /// went on down the chain. The chain variant already refuses a short batch from its own
        /// backend and the session refuses one from here; this is the same rule at the third place
        /// it can happen. A stop is the one exception: a cancelled stage returns what it finished,
        /// and the caller has already been told to expect that.
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

    LinguistExecutiveImpl::LinguistExecutiveImpl(LinguistSpec &spec)
        : LinguistExecutive(spec), m_binding(spec.language(), spec.scheme()),
          m_task([this](const LinguistApi::LinguistConvertInput &input) { return convert(input); },
                 // Reporting and consuming in one step: asked exactly once, after the body has
                 // returned, so a reused executive does not inherit the request (A73).
                 [this] { return m_stopRequested.exchange(false); }) {
        if (const auto import = spec.findImport("linguist/g2p"); import && import->binding()) {
            m_g2pContribution = import->binding()->target().locator();
        }
    }

    LinguistExecutiveImpl::~LinguistExecutiveImpl() {
        // Before any member the body reads is destroyed. The task waits again in its own
        // destructor, but by then the fields below are already gone.
        (void) m_task.waitForFinished();
    }

    const Api::Common::L1::LanguageScheme &LinguistExecutiveImpl::binding() const noexcept {
        return m_binding;
    }

    const srt::ContribLocator &LinguistExecutiveImpl::g2pContribution() const noexcept {
        return m_g2pContribution;
    }

    srt::ITask::State LinguistExecutiveImpl::state() const noexcept {
        return m_task.state();
    }

    srt::Expected<void> LinguistExecutiveImpl::stop() {
        // Cooperative: the conversion loop checks this between stages.
        m_stopRequested = true;
        (void) m_task.stop();

        // And passed down. A stage is handed the whole batch in one call, so a request that
        // stopped at this level would not be answered until that call returned on its own —
        // which for a model or a script is exactly the wait worth interrupting.
        if (auto g2p = m_g2p.load()) {
            (void) g2p->stop();
        }
        if (auto s2p = m_s2p.load()) {
            (void) s2p->stop();
        }
        if (auto onset = m_onset.load()) {
            (void) onset->stop();
        }
        return {};
    }

    srt::Expected<void> LinguistExecutiveImpl::waitForFinished() {
        return m_task.waitForFinished();
    }

    srt::Expected<void> LinguistExecutiveImpl::quit() {
        return stop();
    }

    srt::Expected<void> LinguistExecutiveImpl::wait() {
        return waitForFinished();
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
            auto result =
                createMember<G2PApi::G2PRuntimeOptions, G2PApi::G2PExecutive>("linguist/g2p");
            if (!result) {
                return result.takeError();
            }
            m_g2p = result.take();
        }
        return m_g2p.load();
    }

    srt::Expected<Api::S2P::L1::S2PExecutive *> LinguistExecutiveImpl::s2p() {
        if (!m_s2p.load()) {
            // Absent is legal, the same way an absent onset member is: the composition then
            // reaches the pronunciation layer and no further.
            if (!spec().findImport("linguist/s2p")) {
                return nullptr;
            }
            auto result =
                createMember<S2PApi::S2PRuntimeOptions, S2PApi::S2PExecutive>("linguist/s2p");
            if (!result) {
                return result.takeError();
            }
            m_s2p = result.take();
        }
        return m_s2p.load();
    }

    srt::Expected<Api::Onset::L1::OnsetExecutive *> LinguistExecutiveImpl::onset() {
        if (!m_onset.load()) {
            if (!spec().findImport("linguist/onset")) {
                return nullptr;
            }
            auto result = createMember<OnsetApi::OnsetRuntimeOptions, OnsetApi::OnsetExecutive>(
                "linguist/onset");
            if (!result) {
                return result.takeError();
            }
            m_onset = result.take();
        }
        return m_onset.load();
    }

    srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>>
        LinguistExecutiveImpl::start(const LinguistApi::LinguistConvertInput &input) {
        return m_task.run(input);
    }

    srt::Expected<std::unique_ptr<LinguistApi::LinguistConvertResult>>
        LinguistExecutiveImpl::convert(const LinguistApi::LinguistConvertInput &input) {
        auto result = std::make_unique<LinguistApi::LinguistConvertResult>();
        result->words.resize(input.words.size());

        // A stop that arrived before this call cancels it. stop() means "the conversion running
        // now, or the next one if none is" — an earlier version cleared the flag on entry instead,
        // which silently discarded every cancellation that landed before start(), and that is the
        // ordinary case: a host cancels, then the queued batch begins.
        if (m_stopRequested.load()) {
            return result;
        }

        // Pass one: settle each word's pronunciation layer. Words that arrive with a pinned
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
                output.phonemes = word.locked->phonemes;
                output.onsets = word.locked->onsets;
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
            // Asked again once the stage exists, and only then. A stop that landed after the check
            // above but before the stage was published had no stage to be passed down to, and a
            // stage that then started would run to the end of a batch nothing can interrupt. Now
            // either that stop is seen here, or it arrived after the stage was published and was
            // passed down to it, where a start consumes it.
            if (m_stopRequested.load()) {
                return result;
            }
            auto converted = (*executive)->start(g2pInput);
            if (!converted) {
                return converted.takeError();
            }
            // A stage that reports Canceled while this conversion asked for no stop was cut short
            // by a request left over from an earlier one, which its start has now consumed. The
            // batch is asked for again; a stop meant for this conversion sets the flag here first.
            if ((*executive)->state() == srt::ITask::Canceled && !m_stopRequested.load()) {
                converted = (*executive)->start(g2pInput);
                if (!converted) {
                    return converted.takeError();
                }
            }
            auto words = converted.take();
            if (auto sized = checkBatchSize(words->words.size(), g2pPositions.size(), "g2p");
                !sized && !m_stopRequested) {
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

        if (input.depth == LinguistApi::Depth::Pronunciation || m_stopRequested) {
            // Stopping yields what has been settled so far rather than an error: the task face
            // reports cancellation through state(), which is where ITask already puts it.
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
            // No phoneme member, so the pronunciation layer is as deep as this composition goes.
            // Not an error and not a silently truncated answer: the host reads the limit from
            // LanguageStatus::maxDepth before it asks.
            return result;
        }

        if (!s2pInput.pronunciations.empty()) {
            // Same reason as at the g2p stage: the stage has just been published.
            if (m_stopRequested.load()) {
                return result;
            }
            auto converted = (*phonemeStage)->start(s2pInput);
            if (!converted) {
                return converted.takeError();
            }
            // A stage that reports Canceled while this conversion asked for no stop was cut short
            // by a request left over from an earlier one, which its start has now consumed. The
            // batch is asked for again; a stop meant for this conversion sets the flag here first.
            if ((*phonemeStage)->state() == srt::ITask::Canceled && !m_stopRequested.load()) {
                converted = (*phonemeStage)->start(s2pInput);
                if (!converted) {
                    return converted.takeError();
                }
            }
            auto phonemes = converted.take();
            if (auto sized = checkBatchSize(phonemes->phonemes.size(), s2pPositions.size(), "s2p");
                !sized && !m_stopRequested) {
                return sized.takeError();
            }
            for (std::size_t i = 0; i < s2pPositions.size() && i < phonemes->phonemes.size(); ++i) {
                result->words[s2pPositions[i]].phonemes = phonemes->phonemes[i];
            }
        }

        if (input.depth == LinguistApi::Depth::Phonemes || m_stopRequested) {
            return result;
        }

        // Pass three: onsets. A composition without an onset member is not an error; every
        // position simply reads as not an onset, which is what the contract already means by an
        // uncovered position.
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
            if (m_stopRequested.load()) {
                return result;
            }
            auto marked = (*marker)->start(onsetInput);
            if (!marked) {
                return marked.takeError();
            }
            // A stage that reports Canceled while this conversion asked for no stop was cut short
            // by a request left over from an earlier one, which its start has now consumed. The
            // batch is asked for again; a stop meant for this conversion sets the flag here first.
            if ((*marker)->state() == srt::ITask::Canceled && !m_stopRequested.load()) {
                marked = (*marker)->start(onsetInput);
                if (!marked) {
                    return marked.takeError();
                }
            }
            auto onsets = marked.take();
            if (auto sized = checkBatchSize(onsets->onsets.size(), onsetPositions.size(), "onset");
                !sized && !m_stopRequested) {
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

    srt::Expected<void> LinguistExecutiveImpl::startAsync(
        std::shared_ptr<const LinguistApi::LinguistConvertInput> input, AsyncCallback callback) {
        // The callback runs on the worker thread, as srt::ITask establishes, so a host with a UI
        // thread has to marshal it itself. A null input, a second concurrent execution and an
        // exception escaping the body are all refused or contained by ITask; an earlier version
        // detached a raw thread here and answered waitForFinished() immediately, so a Package
        // could be unloaded out from under a conversion that was still running.
        return m_task.runAsync(std::move(input), std::move(callback));
    }

}
