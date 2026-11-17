#include <functional>
#include <memory>

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
        virtual srt::Expected<void> stop() = 0;
        virtual srt::Expected<void> waitForFinished() = 0;
    };
    class Executive : public wolf::ExecutiveBase<Contract, Input, Result> {
    public:
        Executive() : ExecutiveBase() {
        }
        ~Executive() {
            finish();
        }
        int stops = 0;
        int settled = 0;

    protected:
        Batch runBatch(const Input &) override {
            auto result = std::make_unique<Result>();
            result->stopped = stopRequested();
            return result;
        }
        void onStop() override {
            ++stops;
        }
        void onSettled() override {
            ++settled;
        }
    };
}

BOOST_AUTO_TEST_CASE(stop_is_consumed_after_one_run_and_forwarding_keeps_results) {
    Executive executive;
    BOOST_CHECK(executive.state() == srt::ITask::Idle);
    BOOST_REQUIRE(executive.stop());
    BOOST_CHECK_EQUAL(executive.stops, 1);
    Input input;
    auto cancelled = executive.start(input);
    BOOST_REQUIRE(cancelled);
    BOOST_CHECK(cancelled.value()->stopped);
    BOOST_CHECK(executive.state() == srt::ITask::Canceled);
    BOOST_CHECK_EQUAL(executive.settled, 1);
    auto next = executive.start(input);
    BOOST_REQUIRE(next);
    BOOST_CHECK(!next.value()->stopped);
    BOOST_CHECK(executive.state() == srt::ITask::Succeeded);
    BOOST_CHECK_EQUAL(executive.settled, 2);
    BOOST_REQUIRE(executive.waitForFinished());
}
