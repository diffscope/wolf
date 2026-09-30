#ifndef WOLF_EXECUTIVEBASE_H
#define WOLF_EXECUTIVEBASE_H

#include <atomic>
#include <memory>
#include <utility>

#include <wolf/Support/ExecutiveTask.h>

namespace wolf {

    /// Shared implementation of the task interface of every executive in this repository.
    ///
    /// Each executive exposes start, startAsync, state, stop and waitForFinished and forwards all
    /// of them to one ExecutiveTask. Each executive keeps a stop flag that stop() sets and that
    /// the task consumes after a run has returned. This class implements that task interface.
    /// \a Base is the executive type of the contract (G2PExecutive, S2PExecutive, OnsetExecutive
    /// or LinguistExecutive), and a derived class supplies runBatch() and every member that its
    /// contract declares beyond the task interface, such as initialize().
    ///
    /// \par Stopping
    /// stop() sets the flag, calls onStop() and requests that the task record the stop.
    /// runBatch() reads stopRequested() at its own boundaries and returns the finished part, which
    /// the task then reports as Canceled. After every run, onSettled() is called and the flag is
    /// consumed, so that a stop intended for one run does not cancel the next run.
    ///
    /// \par Destruction
    /// A base class destructor runs after the members of the derived class are destroyed, and
    /// this class therefore cannot wait for a run on their behalf. A derived class whose
    /// runBatch() reads its own members calls finish() at the start of its destructor. This
    /// destructor also waits, which covers a derived class without such members. ExecutiveTask
    /// specifies the same rule.
    ///
    /// This header is internal to the repository and is not installed.
    template <class Base, class Input, class Result>
    class ExecutiveBase : public Base {
    public:
        using Batch = srt::Expected<std::unique_ptr<Result>>;

        srt::Expected<std::unique_ptr<Result>> start(const Input &input) override {
            return m_task.run(input);
        }

        srt::Expected<void> startAsync(std::shared_ptr<const Input> input,
                                       typename Base::AsyncCallback callback) override {
            return m_task.runAsync(std::move(input), std::move(callback));
        }

        srt::ITask::State state() const noexcept override {
            return m_task.state();
        }

        srt::Expected<void> stop() override {
            m_stopRequested = true;
            onStop();
            return m_task.stop();
        }

        srt::Expected<void> waitForFinished() override {
            return m_task.waitForFinished();
        }

    protected:
        template <class... Args>
        explicit ExecutiveBase(Args &&...args)
            : Base(std::forward<Args>(args)...),
              m_task([this](const Input &input) { return runBatch(input); },
                     [this] {
                         onSettled();
                         return m_stopRequested.exchange(false);
                     }) {
        }

        ~ExecutiveBase() {
            (void) m_task.waitForFinished();
        }

        /// Executes the body of one run. It is called on the thread that runs the task and never
        /// during construction or destruction, and the virtual dispatch is therefore always
        /// resolved.
        virtual Batch runBatch(const Input &input) = 0;

        /// Called by stop() after the flag is set and before the task records the stop. An
        /// executive whose runBatch() can block between boundaries interrupts the run here.
        virtual void onStop() {
        }

        /// Called once after every run, before the stop flag is consumed.
        virtual void onSettled() {
        }

        /// Returns whether a stop is pending for the current run.
        bool stopRequested() const noexcept {
            return m_stopRequested.load();
        }

        /// Returns the flag itself, for a callee that polls it at its own boundaries.
        const std::atomic_bool &stopFlag() const noexcept {
            return m_stopRequested;
        }

        /// Waits for a run in progress to return. A derived class whose runBatch() reads its own
        /// members calls this first in its destructor.
        void finish() {
            (void) m_task.waitForFinished();
        }

    private:
        std::atomic_bool m_stopRequested = false;

        /// Declared last, so that it is destroyed first and its wait completes before the flag is
        /// destroyed.
        ExecutiveTask<Input, Result> m_task;
    };

}

#endif // WOLF_EXECUTIVEBASE_H
