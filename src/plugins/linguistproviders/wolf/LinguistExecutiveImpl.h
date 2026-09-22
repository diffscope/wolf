#ifndef WOLF_LINGUISTEXECUTIVEIMPL_H
#define WOLF_LINGUISTEXECUTIVEIMPL_H

#include <atomic>

#include <wolf/Support/ExecutiveTask.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

namespace wolf {

    /// The wolf variant's linguist executive.
    ///
    /// It supervises one G2P, one S2P and, when the composition declares it, one Onset executive,
    /// each created through the import role that names it. Children are created on first use: a
    /// conversion that stops at pronunciations never builds the downstream ones.
    class LinguistExecutiveImpl : public Api::Linguist::L1::LinguistExecutive {
    public:
        explicit LinguistExecutiveImpl(LinguistSpec &spec);
        ~LinguistExecutiveImpl();

        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            start(const Api::Linguist::L1::LinguistConvertInput &input) override;

        srt::Expected<void>
            startAsync(std::shared_ptr<const Api::Linguist::L1::LinguistConvertInput> input,
                       AsyncCallback callback) override;

        srt::ITask::State state() const noexcept override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

        const Api::Common::L1::LanguageScheme &binding() const noexcept override;
        const srt::ContribLocator &g2pContribution() const noexcept override;

    protected:
        /// Stops this executive's own in flight conversion. Children are stopped by the
        /// supervision tree, which walks down from here, so this must not recurse.
        srt::Expected<void> quit() override;

        /// Waits for the conversion to finish. This is what makes a Package unload wait for work
        /// that is still running rather than returning while it is.
        srt::Expected<void> wait() override;

    private:
        /// The body of one conversion. The task face around it settles the state and owns the
        /// worker thread; this only does the work.
        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            convert(const Api::Linguist::L1::LinguistConvertInput &input);

        /// Creates one child through the import named by \a role, giving it runtime options that
        /// carry the target's own variant.
        ///
        /// The variant cannot be hard-coded: the loader compares the whole triple of the options
        /// against the target declaration, and these contracts have many variants.
        template <class Options, class Executive>
        srt::Expected<Executive *> createMember(std::string_view role);

        srt::Expected<Api::G2P::L1::G2PExecutive *> g2p();
        srt::Expected<Api::S2P::L1::S2PExecutive *> s2p();

        /// Returns nullptr when the composition declares no onset member, which is legal: the
        /// host then reads every position as not an onset.
        srt::Expected<Api::Onset::L1::OnsetExecutive *> onset();

        Api::Common::L1::LanguageScheme m_binding;
        srt::ContribLocator m_g2pContribution;

        // Atomic because stop() may arrive on another thread while a conversion is creating
        // them, and stop() has to reach whichever ones already exist.
        std::atomic<Api::G2P::L1::G2PExecutive *> m_g2p = nullptr;
        std::atomic<Api::S2P::L1::S2PExecutive *> m_s2p = nullptr;
        std::atomic<Api::Onset::L1::OnsetExecutive *> m_onset = nullptr;

        std::atomic<bool> m_stopRequested = false;

        /// Declared last so that it is the first member destroyed, and waited on explicitly in the
        /// destructor before anything its body reads goes away.
        ExecutiveTask<Api::Linguist::L1::LinguistConvertInput,
                      Api::Linguist::L1::LinguistConvertResult>
            m_task;
    };

}

#endif // WOLF_LINGUISTEXECUTIVEIMPL_H
