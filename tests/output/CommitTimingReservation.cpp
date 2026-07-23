#include <output/CommitTimingReservation.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

static Monitor::SFixedPresentationPhase phase() {
    return {
        .presentation = Time::steady_tp{10s},
        .refresh      = 10ms,
    };
}

TEST(CommitTimingReservation, rejectsInvalidRefreshPeriod) {
    const Monitor::SFixedPresentationPhase invalid{
        .presentation = Time::steady_tp{10s},
        .refresh      = Time::steady_dur::zero(),
    };

    EXPECT_FALSE(Monitor::nextPresentationSlot(invalid, Time::steady_tp{11s}).has_value());
    EXPECT_FALSE(Monitor::presentationSlotDue(invalid, Time::steady_tp{11s}, Time::steady_tp{11s}));
}

TEST(CommitTimingReservation, earlierFrameOpportunitiesContinue) {
    const auto PHASE  = phase();
    const auto TARGET = PHASE.presentation + 35ms;

    EXPECT_FALSE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 1ms, TARGET));
    EXPECT_FALSE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 11ms, TARGET));
    EXPECT_FALSE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 21ms, TARGET));
}

TEST(CommitTimingReservation, targetBetweenRefreshesUsesFollowingSlot) {
    const auto PHASE  = phase();
    const auto TARGET = PHASE.presentation + 21ms;

    EXPECT_FALSE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 11ms, TARGET));
    EXPECT_TRUE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 21ms, TARGET));
}

TEST(CommitTimingReservation, missedTargetUsesEarliestLaterOpportunity) {
    const auto PHASE  = phase();
    const auto TARGET = PHASE.presentation + 10ms;

    EXPECT_TRUE(Monitor::presentationSlotDue(PHASE, PHASE.presentation + 31ms, TARGET));
}
