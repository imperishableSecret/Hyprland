#include <protocols/CommitTiming.hpp>
#include <protocols/types/ContentUpdate.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(CommitTiming, validatesNanoseconds) {
    EXPECT_TRUE(NCommitTiming::validTimestamp(0));
    EXPECT_TRUE(NCommitTiming::validTimestamp(999'999'999));
    EXPECT_FALSE(NCommitTiming::validTimestamp(1'000'000'000));
    EXPECT_FALSE(NCommitTiming::validTimestamp(UINT32_MAX));
}

TEST(CommitTiming, pastAndPresentTargetsDoNotCreateTimerDelays) {
    const Time::steady_tp NOW{10s};

    EXPECT_FALSE(NCommitTiming::timerDelay(NOW - 1ns, NOW).has_value());
    EXPECT_FALSE(NCommitTiming::timerDelay(NOW, NOW).has_value());
}

TEST(CommitTiming, futureTargetUsesDelayFromCommitTime) {
    const Time::steady_tp TARGET{10s};

    ASSERT_TRUE(NCommitTiming::timerDelay(TARGET, Time::steady_tp{8s}).has_value());
    EXPECT_EQ(*NCommitTiming::timerDelay(TARGET, Time::steady_tp{8s}), 2s);

    ASSERT_TRUE(NCommitTiming::timerDelay(TARGET, Time::steady_tp{9s}).has_value());
    EXPECT_EQ(*NCommitTiming::timerDelay(TARGET, Time::steady_tp{9s}), 1s);
}

TEST(CommitTiming, absoluteTargetIsCapturedByContentUpdate) {
    SSurfaceState state;
    state.commitTimingTarget = Time::steady_tp{10s};

    CContentUpdate update{state, {}};
    state.reset();

    ASSERT_TRUE(update.state().commitTimingTarget.has_value());
    EXPECT_EQ(*update.state().commitTimingTarget, Time::steady_tp{10s});
}
