#include <atomic>
#include <memory>

#include <synthrt/Support/Error.h>
#include <synthrt/Task/ITask.h>

#include <wolf/Support/ExecutiveTask.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    /// The task passes its payloads through as static types, so a test supplies the two.
    class TestInput final : public srt::TaskStartInput {
    public:
        explicit TestInput(int value) : TaskStartInput("com.example.TestInput", 2), value(value) {
        }

        int value;
    };

    class TestResult final : public srt::TaskResult {
    public:
        explicit TestResult(int value) : TaskResult("com.example.TestResult", 3), value(value) {
        }

        int value;
    };

    /// One task and the two things an executive observes about it: the runs it performed and the
    /// settle callbacks it received. `stopRequested` stands for the flag that the executive sets in
    /// stop() and that the callback reads before consuming it.
    class Fixture {
    public:
        Fixture()
            : task([this](const TestInput &input) { return run(input); },
                   [this] { return settle(); }) {
        }

        std::atomic<bool> stopRequested{false};
        std::atomic<int> runs{0};
        std::atomic<int> settled{0};
        bool failNextRun = false;
        wolf::ExecutiveTask<TestInput, TestResult> task;

    private:
        srt::Expected<std::unique_ptr<TestResult>> run(const TestInput &input) {
            ++runs;
            if (failNextRun) {
                failNextRun = false;
                return srt::Error(srt::Error::InvalidFormat, "the run failed");
            }
            return std::make_unique<TestResult>(input.value);
        }

        bool settle() {
            ++settled;
            return stopRequested.exchange(false);
        }
    };

}

/// The base class documents the invariant that onSettled() runs and the stop flag is consumed
/// after every run. A failed run is a run, so a stop that arrived while it was in flight must not
/// survive it: the next run would otherwise report Canceled and the caller would see an empty
/// result for a run that never saw a stop.
BOOST_AUTO_TEST_CASE(the_settle_callback_runs_after_a_failed_run) {
    Fixture fixture;
    fixture.failNextRun = true;
    BOOST_REQUIRE(!fixture.task.run(TestInput(1)));
    BOOST_TEST(fixture.runs.load() == 1);
    BOOST_TEST(fixture.settled.load() == 1);
    BOOST_TEST(fixture.task.state() == srt::ITask::Failed);

    // The same task still runs, and settles again.
    BOOST_REQUIRE(fixture.task.run(TestInput(2)));
    BOOST_TEST(fixture.settled.load() == 2);
    BOOST_TEST(fixture.task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(a_stop_during_a_failed_run_does_not_cancel_the_next_run) {
    Fixture fixture;
    fixture.stopRequested = true;
    fixture.failNextRun = true;
    BOOST_REQUIRE(!fixture.task.run(TestInput(1)));
    BOOST_TEST(fixture.settled.load() == 1);
    // Consumed, so nothing is left for the next run to observe.
    BOOST_TEST(!fixture.stopRequested.load());

    BOOST_REQUIRE(fixture.task.run(TestInput(2)));
    BOOST_TEST(fixture.task.state() == srt::ITask::Succeeded);
}

/// The other half of the same rule: a stop that is pending when a run succeeds does cancel it, and
/// is consumed there as well.
BOOST_AUTO_TEST_CASE(a_stop_during_a_successful_run_reports_canceled) {
    Fixture fixture;
    fixture.stopRequested = true;
    BOOST_REQUIRE(fixture.task.run(TestInput(1)));
    BOOST_TEST(fixture.task.state() == srt::ITask::Canceled);
    BOOST_TEST(fixture.settled.load() == 1);
    BOOST_TEST(!fixture.stopRequested.load());

    BOOST_REQUIRE(fixture.task.run(TestInput(2)));
    BOOST_TEST(fixture.task.state() == srt::ITask::Succeeded);
}
