#ifndef WOLF_EXECUTIVETASK_H
#define WOLF_EXECUTIVETASK_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <synthrt/Task/ITask.h>

namespace wolf {

    /// Task interface of one executive, built on \c srt::ITask.
    ///
    /// Every wolf executive must provide the same five properties: one execution at a time, a
    /// worker thread for \c startAsync, a guard that prevents an exception in the body from
    /// propagating into the framework, a \c Canceled state after a stop request, and a destructor
    /// that does not return while a worker is still executing the body. \c srt::ITask provides all
    /// five, and the inference executives of dsinfer obtain them by holding an ITask and
    /// forwarding to it.
    ///
    /// This class provides the single implementation of that interface in wolf. Each executive
    /// holds an instance and forwards to it, which guarantees that \c waitForFinished() waits,
    /// that \c startAsync runs on a worker, and that a second concurrent execution is rejected.
    ///
    /// The body is supplied as a callable instead of through derivation, so that an executive
    /// holds an instance as a member and requires no companion class per variant. The callables
    /// capture the executive. The member must therefore be declared last, and the destructor of
    /// the executive must call its \c waitForFinished() before any member that the body reads is
    /// destroyed.
    template <class Input, class Result>
    class ExecutiveTask : public srt::ITask {
    public:
        using Body = std::function<srt::Expected<std::unique_ptr<Result>>(const Input &)>;

        /// Returns whether the body observed a stop request. It is called once, after the body
        /// returns, and only to select between \c Canceled and \c Succeeded. A stopped conversion
        /// returns the finished part instead of an error, as the contracts specify.
        using Cancelled = std::function<bool()>;

        using TypedCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        ExecutiveTask(Body body, Cancelled cancelled)
            : m_body(std::move(body)), m_cancelled(std::move(cancelled)) {
        }

        ~ExecutiveTask() = default;

        /// \name Untyped ITask interface
        /// \{
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override {
            // Counted, so that waitForFinished() on another thread also waits for a run on the
            // calling thread and not only for the worker. A package unload reaches an executive
            // through wait(), and the session runs conversions synchronously.
            SyncRun running(*this);
            setState(Running);
            auto result = m_body(static_cast<const Input &>(input));
            if (!result) {
                setState(Failed);
                // The cancelled callback runs after every run, a failed one included: it calls
                // onSettled(), which the base class documents as running once after every run, and
                // it consumes the stop flag. Consuming it here is what keeps a stop that arrived
                // during this run from cancelling the next one.
                (void) m_cancelled();
                return result.takeError();
            }
            setState(m_cancelled() ? Canceled : Succeeded);
            return std::unique_ptr<srt::TaskResult>(result.take().release());
        }

        srt::Expected<void> stop() override {
            // Also recorded for the default asynchronous execution, so that a stop delivered while
            // a worker is running sets the final state to Canceled instead of Succeeded.
            requestAsyncCancellation();
            return {};
        }

        srt::Expected<void> waitForFinished() override {
            waitForAsyncExecution();
            std::unique_lock<std::mutex> lock(m_syncMutex);
            m_syncDone.wait(lock, [this] { return m_syncRuns == 0; });
            return {};
        }
        /// \}

        /// \name Typed interface exposed by the executives
        /// \{
        srt::Expected<std::unique_ptr<Result>> run(const Input &input) {
            auto result = start(static_cast<const srt::TaskStartInput &>(input));
            if (!result) {
                return result.takeError();
            }
            return std::unique_ptr<Result>(static_cast<Result *>(result.take().release()));
        }

        srt::Expected<void> runAsync(std::shared_ptr<const Input> input, TypedCallback callback) {
            if (!callback) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "an asynchronous callback must not be empty");
            }
            // The null input check, the rejection of a second concurrent execution and the
            // exception guard are implemented in ITask::startAsync. This function only restores
            // the static types.
            return ITask::startAsync(
                std::static_pointer_cast<const srt::TaskStartInput>(std::move(input)),
                [callback = std::move(callback)](
                    srt::Expected<std::unique_ptr<srt::TaskResult>> result) mutable {
                    if (!result) {
                        callback(result.takeError());
                        return;
                    }
                    callback(
                        std::unique_ptr<Result>(static_cast<Result *>(result.take().release())));
                });
        }
        /// \}

    private:
        /// Marks one synchronous run for the duration of start().
        class SyncRun {
        public:
            explicit SyncRun(ExecutiveTask &task) : m_task(task) {
                std::lock_guard<std::mutex> lock(m_task.m_syncMutex);
                ++m_task.m_syncRuns;
            }
            ~SyncRun() {
                {
                    std::lock_guard<std::mutex> lock(m_task.m_syncMutex);
                    --m_task.m_syncRuns;
                }
                m_task.m_syncDone.notify_all();
            }

        private:
            ExecutiveTask &m_task;
        };

        Body m_body;
        Cancelled m_cancelled;
        std::mutex m_syncMutex;
        std::condition_variable m_syncDone;
        int m_syncRuns = 0;
    };

}

#endif // WOLF_EXECUTIVETASK_H
