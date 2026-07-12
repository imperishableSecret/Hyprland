#include <output/ScanoutKeepalive.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SScanoutKeepaliveState dueState() {
    return {
        .adaptiveSync     = true,
        .minimumRefreshHz = 40.F,
        .elapsedMillis    = 25.F,
    };
}

TEST(ScanoutKeepalive, requiresAdaptiveSyncAndMinimumRefresh) {
    auto state = dueState();
    EXPECT_TRUE(scanoutKeepaliveDue(state));

    state.adaptiveSync = false;
    EXPECT_FALSE(scanoutKeepaliveDue(state));

    state                  = dueState();
    state.minimumRefreshHz = 0.F;
    EXPECT_FALSE(scanoutKeepaliveDue(state));
}

TEST(ScanoutKeepalive, becomesDueAtTheRefreshDeadline) {
    auto state          = dueState();
    state.elapsedMillis = 24.99F;
    EXPECT_FALSE(scanoutKeepaliveDue(state));

    state.elapsedMillis = 25.F;
    EXPECT_TRUE(scanoutKeepaliveDue(state));

    state.elapsedMillis = 26.F;
    EXPECT_TRUE(scanoutKeepaliveDue(state));
}

TEST(ScanoutKeepalive, pendingOutputWorkSuppressesAnotherKeepalive) {
    auto state            = dueState();
    state.pendingPageFlip = true;
    EXPECT_FALSE(scanoutKeepaliveDue(state));

    state                  = dueState();
    state.pendingIdleFrame = true;
    EXPECT_FALSE(scanoutKeepaliveDue(state));
}

TEST(ScanoutKeepalive, cursorWorkPiggybacksOnTheOneAllowedKeepalive) {
    auto state    = dueState();
    auto decision = scanoutCursorDecision(true, state);
    EXPECT_TRUE(decision.skipCursorSchedule);
    EXPECT_TRUE(decision.scheduleKeepalive);

    state.pendingIdleFrame = true;
    for (size_t i = 0; i < 1000; ++i) {
        decision = scanoutCursorDecision(true, state);
        EXPECT_TRUE(decision.skipCursorSchedule);
        EXPECT_FALSE(decision.scheduleKeepalive);
    }
}

TEST(ScanoutKeepalive, unsuppressedCursorUsesItsOrdinarySchedule) {
    const auto decision = scanoutCursorDecision(false, dueState());
    EXPECT_FALSE(decision.skipCursorSchedule);
    EXPECT_FALSE(decision.scheduleKeepalive);
}
