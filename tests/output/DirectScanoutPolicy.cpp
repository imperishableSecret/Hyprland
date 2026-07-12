#include <output/DirectScanoutPolicy.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SScanoutTestPolicyState validTestState() {
    return {
        .outputAvailable       = true,
        .bufferAvailable       = true,
        .outputEnabled         = true,
        .modeAvailable         = true,
        .attachedBufferMatches = true,
        .bufferSizeMatches     = true,
    };
}

TEST(DirectScanoutPolicy, scanoutTestRequiresACompleteAttachedCandidate) {
    auto state = validTestState();
    EXPECT_TRUE(scanoutTestInputValid(state));

    state.outputAvailable = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
    state                 = validTestState();
    state.bufferAvailable = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
    state               = validTestState();
    state.outputEnabled = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
    state               = validTestState();
    state.modeAvailable = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
    state                       = validTestState();
    state.attachedBufferMatches = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
    state                   = validTestState();
    state.bufferSizeMatches = false;
    EXPECT_FALSE(scanoutTestInputValid(state));
}

static SScanoutDamagePolicyState validDamageState() {
    return {
        .scanoutActive         = true,
        .surfaceMatches        = true,
        .scanoutWindowAlive    = true,
        .solitaryWindowMatches = true,
        .solitaryRootMatches   = true,
    };
}

TEST(DirectScanoutPolicy, onlyTheActiveSolitaryRootBypassesCompositorDamage) {
    auto state = validDamageState();
    EXPECT_TRUE(canBypassCompositorDamage(state));

    state.surfaceMatches = false;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state                    = validDamageState();
    state.scanoutWindowAlive = false;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state                       = validDamageState();
    state.solitaryWindowMatches = false;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state                     = validDamageState();
    state.solitaryRootMatches = false;
    EXPECT_FALSE(canBypassCompositorDamage(state));
}

TEST(DirectScanoutPolicy, blockersRestoreConservativeDamageBookkeeping) {
    auto state       = validDamageState();
    state.hasMirrors = true;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state          = validDamageState();
    state.isMirror = true;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state                = validDamageState();
    state.softwareCursor = true;
    EXPECT_FALSE(canBypassCompositorDamage(state));
    state                      = validDamageState();
    state.captureBlocksScanout = true;
    EXPECT_FALSE(canBypassCompositorDamage(state));
}
