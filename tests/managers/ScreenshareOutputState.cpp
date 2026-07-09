#include <managers/screenshare/ScreenshareManager.hpp>

#include <gtest/gtest.h>

using namespace Screenshare;

TEST(ScreenshareOutputState, idleOutputNeedsNeitherCopyNorScanoutBlock) {
    const CScreenshareManager::SOutputCopyFBState state;
    EXPECT_FALSE(state.needsCopyFB());
    EXPECT_FALSE(state.blocksDirectScanout());
}

TEST(ScreenshareOutputState, monitorAndRegionFramesNeedCopyAndBlockScanout) {
    const CScreenshareManager::SOutputCopyFBState monitorState{.pendingFrames = 1, .pendingMonitorFrames = 1};
    const CScreenshareManager::SOutputCopyFBState regionState{.pendingFrames = 1, .pendingRegionFrames = 1};

    EXPECT_TRUE(monitorState.needsCopyFB());
    EXPECT_TRUE(monitorState.blocksDirectScanout());
    EXPECT_TRUE(regionState.needsCopyFB());
    EXPECT_TRUE(regionState.blocksDirectScanout());
}

TEST(ScreenshareOutputState, windowFrameBlocksScanoutWithoutMirrorCopy) {
    const CScreenshareManager::SOutputCopyFBState state{.pendingFrames = 1};
    EXPECT_FALSE(state.needsCopyFB());
    EXPECT_TRUE(state.blocksDirectScanout());
}

TEST(ScreenshareOutputState, activeSharingBlocksOnlyRepresentedOutputState) {
    const CScreenshareManager::SOutputCopyFBState capturedOutput{.sharingSessions = 1};
    const CScreenshareManager::SOutputCopyFBState otherOutput;

    EXPECT_TRUE(capturedOutput.blocksDirectScanout());
    EXPECT_FALSE(otherOutput.blocksDirectScanout());
}
