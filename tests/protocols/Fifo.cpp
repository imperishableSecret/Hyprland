#include <protocols/Fifo.hpp>

#include <array>
#include <gtest/gtest.h>

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
    EXPECT_EQ(Fifo::watchdogTimeoutMs(0.F), 50);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(-1.F), 50);
}

TEST(Fifo, watchdogUsesSlowestRelevantOutput) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 240.F, .enabled = true},
        Fifo::SWatchdogOutput{.refreshRate = 60.F, .enabled = true},
        Fifo::SWatchdogOutput{.refreshRate = 30.F, .enabled = false},
        Fifo::SWatchdogOutput{.refreshRate = 24.F, .enabled = true, .tearing = true},
    };

    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), 60.F);
    EXPECT_EQ(Fifo::watchdogTimeoutMs(Fifo::slowestRelevantRefresh(OUTPUTS)), 50);
}

TEST(Fifo, watchdogIgnoresTearingAndFallsBackWithoutRelevantOutputs) {
    const std::array OUTPUTS = {
        Fifo::SWatchdogOutput{.refreshRate = 24.F, .enabled = true, .tearing = true},
        Fifo::SWatchdogOutput{.refreshRate = 30.F, .enabled = false},
    };

    EXPECT_EQ(Fifo::slowestRelevantRefresh(OUTPUTS), 60.F);
}

TEST(Fifo, watchdogArmsOnlyForARealQueuedLock) {
    Fifo::CWatchdogState state;

    EXPECT_FALSE(state.lockedQueued(false, true, false).armTimer);
    EXPECT_FALSE(state.lockedQueued(true, false, false).armTimer);
    EXPECT_FALSE(state.lockedQueued(true, true, true).armTimer);
    EXPECT_FALSE(state.armed());

    EXPECT_TRUE(state.lockedQueued(true, true, false).armTimer);
    EXPECT_TRUE(state.armed());
}

TEST(Fifo, presentationCancelsArmedWatchdog) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(true, true, false).armTimer);

    const auto TRANSITION = state.clear(Fifo::WATCHDOG_PRESENTED);
    EXPECT_TRUE(TRANSITION.cancelTimer);
    EXPECT_FALSE(state.armed());
}

TEST(Fifo, unmapAndDestructionCancelOnlyAnArmedWatchdog) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(true, true, false).armTimer);

    const auto UNMAP = state.clear(Fifo::WATCHDOG_UNMAPPED);
    EXPECT_TRUE(UNMAP.cancelTimer);

    const auto DESTROY = state.clear(Fifo::WATCHDOG_DESTROYED);
    EXPECT_FALSE(DESTROY.cancelTimer);
}

TEST(Fifo, watchdogTimeoutRecoversWithoutCancellingExpiredTimer) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(true, true, false).armTimer);

    const auto TIMEOUT = state.clear(Fifo::WATCHDOG_EXPIRED);
    EXPECT_FALSE(TIMEOUT.cancelTimer);
    EXPECT_FALSE(state.armed());

    const auto LATE_PRESENTATION = state.clear(Fifo::WATCHDOG_PRESENTED);
    EXPECT_FALSE(LATE_PRESENTATION.cancelTimer);
    EXPECT_FALSE(state.armed());
}

TEST(Fifo, consecutivePresentationsDoNotRearmWatchdog) {
    Fifo::CWatchdogState state;
    ASSERT_TRUE(state.lockedQueued(true, true, false).armTimer);

    EXPECT_TRUE(state.clear(Fifo::WATCHDOG_PRESENTED).cancelTimer);
    EXPECT_FALSE(state.clear(Fifo::WATCHDOG_PRESENTED).cancelTimer);
    EXPECT_FALSE(state.armed());
}
