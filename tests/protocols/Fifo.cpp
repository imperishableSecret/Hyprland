#include <protocols/Fifo.hpp>

#include <array>
#include <gtest/gtest.h>

TEST(Fifo, submissionWorkRequiresSharedResourceOwner) {
    const auto    UNIQUE_OWNER = makeUnique<int>(1);
    const auto    SHARED_OWNER = makeShared<int>(1);

    const WP<int> UNIQUE_HANDLE = UNIQUE_OWNER;
    const WP<int> SHARED_HANDLE = SHARED_OWNER;

    EXPECT_FALSE(UNIQUE_HANDLE.lock());
    EXPECT_TRUE(SHARED_HANDLE.lock());
}

TEST(Fifo, watchdogUsesThreeRefreshIntervals) {
    EXPECT_EQ(Fifo::watchdogTimeoutMs(30.F), 100);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(24.F), 125);
}

TEST(Fifo, watchdogClampsToConservativeRange) {
    EXPECT_EQ(Fifo::watchdogTimeoutMs(60.F), 50);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(240.F), 50);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(10.F), 250);
}

TEST(Fifo, watchdogUsesSafeFallbackForInvalidRefresh) {
    EXPECT_EQ(Fifo::watchdogTimeoutMs(0.F), 125);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(-1.F), 125);
}

TEST(Fifo, watchdogUsesSlowestRelevantOutput) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 240.F, .enabled = true},
        Fifo::SWatchdogOutput{.refreshRate = 60.F, .enabled = true},
        Fifo::SWatchdogOutput{.refreshRate = 30.F, .enabled = false},
        Fifo::SWatchdogOutput{.refreshRate = 24.F, .enabled = true, .tearing = true},
    };

    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), 60.F);
}

TEST(Fifo, watchdogUsesVrrFloorAsRecoveryRate) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 119.F, .vrrMinHz = 24.F, .enabled = true, .vrr = true},
    };

    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), 24.F);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(Fifo::slowestRelevantRefresh(OUTPUTS)), 125);
}

TEST(Fifo, watchdogUsesConservativeFallbackForUnknownVrrFloor) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 119.F, .enabled = true, .vrr = true},
    };

    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), Fifo::WATCHDOG_FALLBACK_REFRESH_HZ);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(Fifo::slowestRelevantRefresh(OUTPUTS)), 125);
}

TEST(Fifo, watchdogIgnoresTearingAndFallsBackWithoutRelevantOutputs) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 24.F, .enabled = true, .tearing = true},
        Fifo::SWatchdogOutput{.refreshRate = 30.F, .enabled = false},
    };
    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), Fifo::WATCHDOG_FALLBACK_REFRESH_HZ);
}

TEST(Fifo, watchdogBelongsOnlyToFrontEpoch) {
    Fifo::CWatchdogState state;
    EXPECT_FALSE(state.lockedQueued(0, true).armTimer);
    EXPECT_FALSE(state.lockedQueued(1, false).armTimer);
    EXPECT_TRUE(state.lockedQueued(1, true).armTimer);
    EXPECT_FALSE(state.lockedQueued(2, true).armTimer);
    EXPECT_TRUE(state.armed());
    EXPECT_EQ(state.epoch(), 1);
}

TEST(Fifo, submissionCancelsMatchingWatchdog) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(4, true).armTimer);
    EXPECT_TRUE(state.clear(4, Fifo::WATCHDOG_SUBMITTED).cancelTimer);
    EXPECT_FALSE(state.armed());
}

TEST(Fifo, watchdogTimeoutDoesNotCancelExpiredTimer) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(4, true).armTimer);
    EXPECT_FALSE(state.clear(4, Fifo::WATCHDOG_EXPIRED).cancelTimer);
    EXPECT_FALSE(state.armed());
    EXPECT_FALSE(state.clear(4, Fifo::WATCHDOG_SUBMITTED).cancelTimer);
}

TEST(Fifo, lateEpochCannotClearCurrentWatchdog) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(10, true).armTimer);
    EXPECT_FALSE(state.clear(9, Fifo::WATCHDOG_SUBMITTED).cancelTimer);
    EXPECT_TRUE(state.armed());
    EXPECT_EQ(state.epoch(), 10);
}

TEST(Fifo, submissionReleasesOnlyTheActiveEpoch) {
    EXPECT_TRUE(Fifo::submissionMatchesActiveEpoch(12, 12));
    EXPECT_FALSE(Fifo::submissionMatchesActiveEpoch(12, 11));
    EXPECT_FALSE(Fifo::submissionMatchesActiveEpoch(12, 13));
    EXPECT_FALSE(Fifo::submissionMatchesActiveEpoch(0, 0));
}

TEST(Fifo, waitUsesNewestQueuedBarrierBeforeCurrentStateCatchesUp) {
    EXPECT_EQ(Fifo::waitBarrierEpoch(7, false, 0), 7);
    EXPECT_EQ(Fifo::waitBarrierEpoch(7, true, 6), 7);
    EXPECT_EQ(Fifo::waitBarrierEpoch(0, true, 6), 6);
    EXPECT_EQ(Fifo::waitBarrierEpoch(0, false, 6), 0);
}

TEST(Fifo, synchronizedSubsurfaceIgnoresWaitConstraint) {
    EXPECT_TRUE(Fifo::waitConstraintApplies(7, false));
    EXPECT_FALSE(Fifo::waitConstraintApplies(7, true));
    EXPECT_FALSE(Fifo::waitConstraintApplies(0, false));
}

TEST(Fifo, watchdogOnlyCoversAnActiveMatchingBarrier) {
    EXPECT_TRUE(Fifo::watchdogEpochEligible(true, true, true, 7, 7));
    EXPECT_FALSE(Fifo::watchdogEpochEligible(false, true, true, 7, 7));
    EXPECT_FALSE(Fifo::watchdogEpochEligible(true, false, true, 7, 7));
    EXPECT_FALSE(Fifo::watchdogEpochEligible(true, true, false, 7, 7));
    EXPECT_FALSE(Fifo::watchdogEpochEligible(true, true, true, 6, 7));
}
