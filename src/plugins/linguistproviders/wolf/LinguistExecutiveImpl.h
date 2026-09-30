#ifndef WOLF_LINGUISTEXECUTIVEIMPL_H
#define WOLF_LINGUISTEXECUTIVEIMPL_H

#include <atomic>

#include <wolf/Support/ExecutiveBase.h>

#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

namespace wolf {

    /// Linguist executive of the wolf variant.
    ///
    /// The executive supervises one G2P executive and, if the composition declares the
    /// corresponding imports, one S2P executive and one Onset executive. Each child is created on
    /// first use through the import role that names it, so a conversion that ends at the
    /// pronunciation layer never creates the S2P and Onset executives.
    class LinguistExecutiveImpl : public ExecutiveBase<Api::Linguist::L1::LinguistExecutive,
                                                       Api::Linguist::L1::LinguistConvertInput,
                                                       Api::Linguist::L1::LinguistConvertResult> {
    public:
        explicit LinguistExecutiveImpl(LinguistSpec &spec);
        ~LinguistExecutiveImpl();

        const Api::Common::L1::LanguageScheme &binding() const noexcept override;
        const srt::ContribLocator &g2pContribution() const noexcept override;

    protected:
        /// Stops the in-flight conversion of this executive. The supervision tree stops the
        /// children by walking down from this executive, so this function must not recurse.
        srt::Expected<void> quit() override;

        /// Waits for the conversion to finish, so that unloading a Package waits for running work
        /// instead of returning while the work is still running.
        srt::Expected<void> wait() override;

        /// Runs one conversion. The ExecutiveTask behind this executive sets the task state and
        /// owns the worker thread; this function performs only the conversion.
        Batch runBatch(const Api::Linguist::L1::LinguistConvertInput &input) override;

        /// Forwards a stop request to the existing members. Each stage receives the whole batch in
        /// one call, so a stop request that is not forwarded takes effect only after that call
        /// returns. For a model or a script, that call is the wait that most needs interruption.
        void onStop() override;

    private:
        /// Runs one stage over its batch. All three stages use this function.
        ///
        /// A stage that reports Canceled although this conversion has no pending stop request is
        /// run once more, because the stop request that canceled it was left over from an earlier
        /// conversion.
        ///
        /// \return A null result, without starting the stage, if a stop request is pending after
        /// the stage has been created; the caller then returns the results completed so far. The
        /// error of the stage if the stage fails. Otherwise, the result of the stage.
        template <class Stage, class Input>
        auto runStage(Stage &stage, const Input &input) -> decltype(stage.start(input));

        /// Creates one child through the import named by \a role, with runtime options that
        /// contain the variant of the import target.
        ///
        /// The variant cannot be hard-coded: the loader compares the whole triple of the options
        /// against the target declaration, and these contracts have many variants.
        ///
        /// \return The created child, or an error if the import is not bound or the child cannot
        /// be created.
        template <class Options, class Executive>
        srt::Expected<Executive *> createMember(std::string_view role);

        srt::Expected<Api::G2P::L1::G2PExecutive *> g2p();
        srt::Expected<Api::S2P::L1::S2PExecutive *> s2p();

        /// Returns the Onset executive and creates it on first use.
        ///
        /// \return nullptr if the composition declares no onset import, which is valid; every
        /// position is then marked as not an onset. An error if the executive cannot be created.
        /// Otherwise, the Onset executive.
        srt::Expected<Api::Onset::L1::OnsetExecutive *> onset();

        Api::Common::L1::LanguageScheme m_binding;
        srt::ContribLocator m_g2pContribution;

        // Atomic because stop() may be called on another thread while a conversion is creating
        // the members, and stop() must reach every member that already exists.
        std::atomic<Api::G2P::L1::G2PExecutive *> m_g2p = nullptr;
        std::atomic<Api::S2P::L1::S2PExecutive *> m_s2p = nullptr;
        std::atomic<Api::Onset::L1::OnsetExecutive *> m_onset = nullptr;
    };

}

#endif // WOLF_LINGUISTEXECUTIVEIMPL_H
