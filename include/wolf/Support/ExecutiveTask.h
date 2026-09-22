#ifndef WOLF_EXECUTIVETASK_H
#define WOLF_EXECUTIVETASK_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <synthrt/Task/ITask.h>

namespace wolf {

    /// The task face of one executive, built on \c srt::ITask.
    ///
    /// Every wolf executive owes the same five things: one execution at a time, a worker thread
    /// for \c startAsync, a guard so an exception in the body does not escape into the framework,
    /// a \c Canceled state when a stop was seen, and a destructor that does not return while a
    /// worker is still inside the body. \c srt::ITask already provides all five, and dsinfer's
    /// inference executives get them by holding one and forwarding.
    ///
    /// This class exists so that wolf has exactly one copy of that face. Each executive holds one
    /// and forwards to it, which is what makes \c waitForFinished() really wait, \c startAsync
    /// really run on a worker, and a second concurrent execution really be refused.
    ///
    /// The body is supplied as a callable rather than by deriving, so an executive holds one of
    /// these as a member and does not need a companion class per variant. The callables capture
    /// the executive, so the member must be declared last and its \c waitForFinished() called from
    /// the executive's destructor before anything the body reads goes away.
    template <class Input, class Result>
    class ExecutiveTask : public srt::ITask {
    public:
        using Body = std::function<srt::Expected<std::unique_ptr<Result>>(const Input &)>;

        /// Answers whether the body saw a stop request. Asked once, after the body returns, and
        /// only to choose between \c Canceled and \c Succeeded. A stopped conversion returns what
        /// it finished rather than an error, which is what the contracts already say.
        using Cancelled = std::function<bool()>;

        using TypedCallback = std::function<void(srt::Expected<std::unique_ptr<Result>> result)>;

        ExecutiveTask(Body body, Cancelled cancelled)
            : m_body(std::move(body)), m_cancelled(std::move(cancelled)) {
        }

        ~ExecutiveTask() = default;

        /// \name The untyped ITask face
        /// \{
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override {
            // Counted, so that waitForFinished() from another thread really waits for a run made
            // on the caller's thread and not only for the worker. A package unload reaches an
            // executive through wait(), and the session runs conversions synchronously.
            SyncRun running(*this);
            setState(Running);
            auto result = m_body(static_cast<const Input &>(input));
            if (!result) {
                setState(Failed);
                return result.takeError();
            }
            setState(m_cancelled() ? Canceled : Succeeded);
            return std::unique_ptr<srt::TaskResult>(result.take().release());
        }

        srt::Expected<void> stop() override {
            // Recorded for the default asynchronous execution as well, so that a stop delivered
            // while a worker is running settles the state as Canceled rather than Succeeded.
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

        /// \name The typed face the executives expose
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
            // The null input check, the refusal of a second concurrent execution and the exception
            // guard all live in ITask::startAsync; this only puts the types back on.
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
