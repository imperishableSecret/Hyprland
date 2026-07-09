#include <output/FrameCompletionDecision.hpp>
#include <output/MonitorFrameScheduler.hpp>

#include <gtest/gtest.h>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace Monitor;

TEST(FrameCompletionDecision, ignoresFenceWhenNewSchedulingIsDisabled) {
    EXPECT_EQ(frameCompletionAction(false, false, false), FRAME_COMPLETION_IGNORE);
    EXPECT_EQ(frameCompletionAction(false, true, true), FRAME_COMPLETION_IGNORE);
}

TEST(FrameCompletionDecision, waitsForValidFence) {
    EXPECT_EQ(frameCompletionAction(true, true, false), FRAME_COMPLETION_WAIT);
    EXPECT_EQ(frameCompletionAction(true, true, true), FRAME_COMPLETION_WAIT);
}

TEST(FrameCompletionDecision, invalidFenceFallsBackAndPreservesPendingCommit) {
    EXPECT_EQ(frameCompletionAction(true, false, false), FRAME_COMPLETION_FALLBACK);
    EXPECT_EQ(frameCompletionAction(true, false, true), FRAME_COMPLETION_FALLBACK_COMMIT_PENDING);
}

TEST(FrameCompletionDecision, alreadySignalledFenceUsesReadableWaitPath) {
    const int FD = eventfd(1, EFD_CLOEXEC | EFD_NONBLOCK);
    ASSERT_GE(FD, 0);

    pollfd descriptor = {.fd = FD, .events = POLLIN};
    EXPECT_EQ(poll(&descriptor, 1, 0), 1);
    EXPECT_TRUE(descriptor.revents & POLLIN);
    EXPECT_EQ(frameCompletionAction(true, true, false), FRAME_COMPLETION_WAIT);

    close(FD);
}

TEST(FrameCompletionDecision, schedulerClockIsMonotonic) {
    static_assert(CMonitorFrameScheduler::hrc::is_steady);
}
