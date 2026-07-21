#include <output/DirectScanoutPolicy.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SDirectScanoutDamageState bypassState() {
    return {
        .exactActiveRoot = true,
    };
}

TEST(DirectScanoutPolicy, requiresExactActiveRootIdentity) {
    auto state = bypassState();
    EXPECT_TRUE(canBypassCompositorDamage(state));

    state.exactActiveRoot = false;
    EXPECT_FALSE(canBypassCompositorDamage(state));
}

TEST(DirectScanoutPolicy, compositorBlockersDisableBypass) {
    auto state     = bypassState();
    state.blockers = DS_DAMAGE_BLOCK_MIRROR;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state          = bypassState();
    state.blockers = DS_DAMAGE_BLOCK_SOFTWARE_CURSOR;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state          = bypassState();
    state.blockers = DS_DAMAGE_BLOCK_GLOBAL_CAPTURE;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state          = bypassState();
    state.blockers = DS_DAMAGE_BLOCK_MIRROR | DS_DAMAGE_BLOCK_SOFTWARE_CURSOR | DS_DAMAGE_BLOCK_GLOBAL_CAPTURE;
    EXPECT_FALSE(canBypassCompositorDamage(state));
}
