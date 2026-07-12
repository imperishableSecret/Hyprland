#include <output/DirectScanoutCandidate.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SDirectScanoutSnapshot validSnapshot() {
    return {
        .windowIdentity           = 1,
        .surfaceIdentity          = 2,
        .bufferIdentity           = 3,
        .bufferSize               = {3840, 2160},
        .transform                = WL_OUTPUT_TRANSFORM_NORMAL,
        .format                   = 0x34325258,
        .modifier                 = 4,
        .colorManagementIdentity  = 5,
        .imageDescriptionIdentity = 6,
        .contentType              = 7,
        .dmaBuffer                = true,
        .surfaceIsHDR             = true,
        .surfaceIsScRGB           = false,
    };
}

TEST(DirectScanoutCandidate, unchangedSnapshotRemainsValid) {
    const auto snapshot = validSnapshot();
    EXPECT_TRUE(directScanoutSnapshotMatches(snapshot, snapshot));
}

TEST(DirectScanoutCandidate, staleSnapshotRejectsEveryCommitRelevantChange) {
    const auto expected = validSnapshot();
    auto       current  = expected;

    const auto EXPECT_STALE = [&] {
        EXPECT_FALSE(directScanoutSnapshotMatches(expected, current));
        current = expected;
    };

    ++current.windowIdentity;
    EXPECT_STALE();
    ++current.surfaceIdentity;
    EXPECT_STALE();
    ++current.bufferIdentity;
    EXPECT_STALE();
    ++current.bufferSize.x;
    EXPECT_STALE();
    ++current.transform;
    EXPECT_STALE();
    ++current.format;
    EXPECT_STALE();
    ++current.modifier;
    EXPECT_STALE();
    ++current.colorManagementIdentity;
    EXPECT_STALE();
    ++current.imageDescriptionIdentity;
    EXPECT_STALE();
    ++current.contentType;
    EXPECT_STALE();
    current.dmaBuffer = false;
    EXPECT_STALE();
    current.surfaceIsHDR = false;
    EXPECT_STALE();
    current.surfaceIsScRGB = true;
    EXPECT_STALE();
}
