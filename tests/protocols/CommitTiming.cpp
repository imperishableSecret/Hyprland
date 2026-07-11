#include <protocols/CommitTiming.hpp>

#include <chrono>
#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(CommitTiming, frameUsesNextPresentationPhase) {
    const auto LAST_PRESENTATION = Time::steady_tp{} + 100ms;
    const auto NOW               = Time::steady_tp{} + 101ms;

    EXPECT_EQ(CommitTiming::nextPresentation(LAST_PRESENTATION, NOW, 8ms), Time::steady_tp{} + 108ms);
    EXPECT_EQ(CommitTiming::nextPresentation(LAST_PRESENTATION, NOW, 8ms, 1), Time::steady_tp{} + 116ms);
}

TEST(CommitTiming, mesaSafetyMarginAt119HzUsesWholeFrame) {
    const auto INTERVAL          = std::chrono::nanoseconds(8403361);
    const auto LAST_PRESENTATION = Time::steady_tp{} + 100ms;
    const auto NOW               = LAST_PRESENTATION + 1ms;
    const auto TARGET            = LAST_PRESENTATION + INTERVAL - 500us;

    EXPECT_EQ(CommitTiming::nextPresentation(LAST_PRESENTATION, NOW, INTERVAL), LAST_PRESENTATION + INTERVAL);
    EXPECT_EQ(CommitTiming::frameWakeTime(LAST_PRESENTATION, TARGET, INTERVAL, NOW), NOW);
}

TEST(CommitTiming, idleOutputAdvancesToNextFuturePhase) {
    const auto LAST_PRESENTATION = Time::steady_tp{} + 100ms;
    const auto NOW               = Time::steady_tp{} + 125ms;

    EXPECT_EQ(CommitTiming::nextPresentation(LAST_PRESENTATION, NOW, 8ms), Time::steady_tp{} + 132ms);
}

TEST(CommitTiming, wakeTargetsPhaseBeforeEligiblePresentation) {
    const auto LAST_PRESENTATION = Time::steady_tp{} + 100ms;
    const auto TARGET            = Time::steady_tp{} + 123ms + 500us;
    const auto NOW               = Time::steady_tp{} + 101ms;

    EXPECT_EQ(CommitTiming::frameWakeTime(LAST_PRESENTATION, TARGET, 8ms, NOW), Time::steady_tp{} + 116ms + 100us);
}

TEST(CommitTiming, imminentTargetWakesImmediately) {
    const auto LAST_PRESENTATION = Time::steady_tp{} + 100ms;
    const auto TARGET            = Time::steady_tp{} + 107ms + 500us;
    const auto NOW               = Time::steady_tp{} + 101ms;

    EXPECT_EQ(CommitTiming::frameWakeTime(LAST_PRESENTATION, TARGET, 8ms, NOW), NOW);
}

TEST(CommitTiming, unknownOutputPhaseNeverReleasesEarly) {
    const auto TARGET = Time::steady_tp{} + 120ms;
    const auto NOW    = Time::steady_tp{} + 100ms;

    EXPECT_EQ(CommitTiming::nextPresentation({}, NOW, 8ms), NOW);
    EXPECT_EQ(CommitTiming::frameWakeTime({}, TARGET, 8ms, NOW), TARGET);
}

TEST(CommitTiming, elapsedTimestampRemainsAConstraintForTheNextCommit) {
    const auto NOW = Time::steady_tp{} + 100ms;

    EXPECT_EQ(CommitTiming::clampTarget(NOW, -10ms), NOW);
    EXPECT_EQ(CommitTiming::clampTarget(NOW, 10ms), NOW + 10ms);
}

TEST(CommitTiming, fixedRefreshCanReleaseForTheEligiblePresentation) {
    const auto NOW      = Time::steady_tp{} + 100ms;
    const auto EXPECTED = Time::steady_tp{} + 108ms;

    EXPECT_TRUE(CommitTiming::presentationEligible(Time::steady_tp{} + 107ms, NOW, EXPECTED, false));
    EXPECT_FALSE(CommitTiming::presentationEligible(Time::steady_tp{} + 109ms, NOW, EXPECTED, false));
}

TEST(CommitTiming, variableAndTearingPathsNeverReleaseBeforeTarget) {
    const auto NOW      = Time::steady_tp{} + 100ms;
    const auto EXPECTED = Time::steady_tp{} + 108ms;

    EXPECT_FALSE(CommitTiming::presentationEligible(Time::steady_tp{} + 107ms, NOW, EXPECTED, true));
    EXPECT_TRUE(CommitTiming::presentationEligible(NOW, NOW, EXPECTED, true));
}

TEST(CommitTiming, upcomingTearingFrameUsesVariableTimingBeforeTransition) {
    EXPECT_FALSE(CommitTiming::variableTiming(false, false, false));
    EXPECT_TRUE(CommitTiming::variableTiming(true, false, false));
    EXPECT_TRUE(CommitTiming::variableTiming(false, true, false));
    EXPECT_TRUE(CommitTiming::variableTiming(false, false, true));
}
