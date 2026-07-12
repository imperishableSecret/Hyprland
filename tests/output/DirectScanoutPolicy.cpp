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
