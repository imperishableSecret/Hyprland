#include <animation/AnimationTickPacer.hpp>

#include <gtest/gtest.h>

using namespace Animation;

TEST(AnimationTickPacer, pacesCommonRefreshRates) {
    CAnimationTickPacer hz60;
    CAnimationTickPacer hz144;
    CAnimationTickPacer hz240;
    hz60.considerOutput(60.F);
    hz144.considerOutput(144.F);
    hz240.considerOutput(240.F);

    EXPECT_EQ(hz60.interval(), std::chrono::microseconds(16667));
    EXPECT_EQ(hz144.interval(), std::chrono::microseconds(6945));
    EXPECT_EQ(hz240.interval(), std::chrono::microseconds(4167));
}

TEST(AnimationTickPacer, mixedRefreshUsesFastestEnabledOutput) {
    CAnimationTickPacer pacer;
    pacer.considerOutput(60.F);
    pacer.considerOutput(144.F, false);
    pacer.considerOutput(240.F);

    EXPECT_EQ(pacer.interval(), std::chrono::microseconds(4167));
}

TEST(AnimationTickPacer, invalidRefreshUsesSafeFallback) {
    CAnimationTickPacer pacer;
    pacer.considerOutput(0.F);
    pacer.considerOutput(-1.F);

    EXPECT_EQ(pacer.interval(), std::chrono::microseconds(16667));
}
