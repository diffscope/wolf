#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>

#include <synthrt/Task/ITask.h>

#include <executiveutil/ExecutiveBase.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    class Input : public srt::TaskStartInput {
    public:
        Input() : TaskStartInput("test", 1) {
        }
    };
    class Result : public srt::TaskResult {
    public:
        Result() : TaskResult("test", 1) {
        }
        bool stopped = false;
    };
    class Contract {
    public:
        using AsyncCallback = std::function<void(srt::Expected<std::unique_ptr<Result>>)>;
        virtual ~Contract() = default;
        virtual srt::Expected<std::unique_ptr<Result>> start(const Input &) = 0;
        virtual srt::Expected<void> startAsync(std::shared_ptr<const Input>, AsyncCallback) = 0;
        virtual srt::ITask::State state() const noexcept = 0;

        srt::Expected<void> stop() {
            return quit();
        }

        srt::Expected<void> waitForFinished() {
            return wait();
        }

    protected:
        virtual srt::Expected<void> quit() = 0;
        virtual srt::Expected<void> wait() = 0;
    };
    class Executive : public wolf::ExecutiveBase<Contract, Input, Result> {
    public:
        Executive() : ExecutiveBase() {
        }
        ~Executive() {
            (void) quit();
            (void) wait();
        }
        int stops = 0;
        int settled = 0;
        bool block = false;
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false;

    protected:
        Batch runBatch(const Input &) override {
            if (block) {
                std::unique_lock lock(mutex);
                entered = true;
                changed.notify_all();
                changed.wait(lock, [this] { return stopRequested(); });
            }
            auto result = std::make_unique<Result>();
            result->stopped = stopRequested();
            return result;
        }
        void onStop() override {
            ++stops;
            changed.notify_all();
        }
        void onSettled() override {
            ++settled;
        }
    };

}

BOOST_AUTO_TEST_CASE(stop_while_idle_does_not_cancel_the_next_run) {
    Executive executive;
    BOOST_CHECK(executive.state() == srt::ITask::Idle);
    BOOST_REQUIRE(executive.stop());
    BOOST_CHECK_EQUAL(executive.stops, 0);
    BOOST_CHECK(executive.state() == srt::ITask::Idle);
    Input input;
    auto result = executive.start(input);
    BOOST_REQUIRE(result);
    BOOST_CHECK(!result.value()->stopped);
    BOOST_CHECK(executive.state() == srt::ITask::Succeeded);
    BOOST_CHECK_EQUAL(executive.settled, 1);
}

BOOST_AUTO_TEST_CASE(stop_during_a_run_is_consumed_after_that_run) {
    Executive executive;
    executive.block = true;
    std::promise<srt::Expected<std::unique_ptr<Result>>> promise;
    auto future = promise.get_future();
    BOOST_REQUIRE(
        executive.startAsync(std::make_shared<Input>(),
                             [&promise](srt::Expected<std::unique_ptr<Result>> result) mutable {
                                 promise.set_value(std::move(result));
                             }));
    {
        std::unique_lock lock(executive.mutex);
        BOOST_REQUIRE(executive.changed.wait_for(lock, std::chrono::seconds(10),
                                                 [&] { return executive.entered; }));
    }

    BOOST_REQUIRE(executive.stop());
    auto cancelled = future.get();
    BOOST_REQUIRE(cancelled);
    BOOST_CHECK(cancelled.value()->stopped);
    BOOST_CHECK(executive.state() == srt::ITask::Canceled);
    BOOST_CHECK_EQUAL(executive.stops, 1);
    BOOST_CHECK_EQUAL(executive.settled, 1);

    // A restart while the callback is still being delivered is refused by contract, and the
    // promise above only proves that the callback has begun. The barrier is what makes the
    // restart below deterministic.
    BOOST_REQUIRE(executive.waitForFinished());

    executive.block = false;
    Input input;
    auto next = executive.start(input);
    BOOST_REQUIRE(next);
    BOOST_CHECK(!next.value()->stopped);
    BOOST_CHECK(executive.state() == srt::ITask::Succeeded);
    BOOST_CHECK_EQUAL(executive.settled, 2);
    BOOST_REQUIRE(executive.waitForFinished());
}
