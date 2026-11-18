#ifndef WOLF_EXECUTIVEUTIL_EXECUTIVETASK_H
#define WOLF_EXECUTIVEUTIL_EXECUTIVETASK_H

#include <functional>
#include <memory>
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

        /// \name Typed interface exposed by the executives
        /// \{
        srt::Expected<std::unique_ptr<Result>> run(const Input &input) {
            auto result = ITask::start(input);
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
            return srt::ITask::startAsync(
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

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input) override {
            return m_body(static_cast<const Input &>(input));
        }

        State classifyOutcome(
            const srt::Expected<std::unique_ptr<srt::TaskResult>> &outcome) noexcept override {
            const bool stopped = m_cancelled();
            if (!outcome) {
                return Failed;
            }
            return stopped ? Canceled : Succeeded;
        }

    private:
        Body m_body;
        Cancelled m_cancelled;
    };

}

#endif // WOLF_EXECUTIVEUTIL_EXECUTIVETASK_H
