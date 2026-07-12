#include <protocols/types/SurfaceState.hpp>

#include <gtest/gtest.h>

TEST(SurfaceStateDamage, preAccumulationPreservesDamageForScanoutCommit) {
    SSurfaceState state;
    state.bufferSize   = {1920, 1080};
    state.damage       = CBox{10, 10, 20, 20};
    state.bufferDamage = CBox{100, 100, 10, 10};

    auto PRECONSUMED = state.accumulateBufferDamage();
    auto KMS_DAMAGE  = state.accumulateBufferDamage();

    EXPECT_TRUE(state.damage.empty());
    EXPECT_EQ(PRECONSUMED.getRects().size(), 2U);
    EXPECT_EQ(KMS_DAMAGE.getRects().size(), 2U);
    EXPECT_EQ(PRECONSUMED.getExtents(), KMS_DAMAGE.getExtents());
}

TEST(SurfaceStateDamage, nextCommitWithoutDamageClearsAccumulatedState) {
    SSurfaceState current;
    current.bufferDamage = CBox{0, 0, 100, 100};

    SSurfaceState pending;
    current.updateFrom(pending);

    EXPECT_TRUE(current.damage.empty());
    EXPECT_TRUE(current.bufferDamage.empty());
}
