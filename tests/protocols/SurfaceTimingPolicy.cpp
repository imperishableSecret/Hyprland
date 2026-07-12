#include <protocols/core/SurfaceTimingPolicy.hpp>

#include <gtest/gtest.h>

using namespace SurfaceTimingPolicy;

TEST(SurfaceTimingPolicy, discardedFeedbackIsConsumedBeforeRouting) {
    EXPECT_EQ(workAction(true, true, true), eWorkAction::DISCARD);
}

TEST(SurfaceTimingPolicy, framesWithoutProtocolWorkSkipOutputSelection) {
    EXPECT_EQ(workAction(false, false, false), eWorkAction::SKIP);
    EXPECT_EQ(workAction(false, true, false), eWorkAction::ROUTE);
    EXPECT_EQ(workAction(false, false, true), eWorkAction::ROUTE);
}

TEST(SurfaceTimingPolicy, oneEligibleEnteredOutputUsesTheFastPath) {
    EXPECT_TRUE(canUseSingleEnteredOutput(1, true));
    EXPECT_FALSE(canUseSingleEnteredOutput(1, false));
    EXPECT_FALSE(canUseSingleEnteredOutput(2, true));
}

TEST(SurfaceTimingPolicy, largestVisibleAreaWins) {
    EXPECT_TRUE(candidatePreferred(200.0, 100.0, false));
    EXPECT_FALSE(candidatePreferred(100.0, 200.0, true));
}

TEST(SurfaceTimingPolicy, previousOutputWinsAnAreaTie) {
    EXPECT_FALSE(candidatePreferred(100.0, 100.0, false));
    EXPECT_TRUE(candidatePreferred(100.0, 100.0, true));
}
