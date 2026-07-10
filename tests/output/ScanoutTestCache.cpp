#include <output/ScanoutTestCache.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SScanoutTestState baseState() {
    return {
        .bufferId         = 1,
        .modeId           = 2,
        .customModeId     = 3,
        .bufferFormat     = 4,
        .outputFormat     = 5,
        .bufferModifier   = 6,
        .ctmHash          = 7,
        .hdrMetadataHash  = 8,
        .gammaLutHash     = 9,
        .degammaLutHash   = 10,
        .modeWidth        = 1920,
        .modeHeight       = 1080,
        .modeRefreshRate  = 60000,
        .presentationMode = 11,
        .transform        = 12,
        .contentType      = 13,
        .enabled          = true,
        .adaptiveSync     = true,
        .wideColorGamut   = true,
    };
}

TEST(ScanoutTestCache, skipsOnlyPreviouslyAcceptedCleanState) {
    CScanoutTestCache cache;
    const auto        STATE = baseState();

    EXPECT_FALSE(cache.canSkip(STATE, false, false));
    cache.accept(STATE);
    EXPECT_TRUE(cache.canSkip(STATE, false, false));
    EXPECT_FALSE(cache.canSkip(STATE, true, false));
    EXPECT_FALSE(cache.canSkip(STATE, false, true));

    cache.invalidate();
    EXPECT_FALSE(cache.canSkip(STATE, false, false));
}

TEST(ScanoutTestCache, invalidatesEveryTrackedAtomicConstraint) {
    CScanoutTestCache cache;
    const auto        STATE = baseState();
    cache.accept(STATE);

    auto changed     = STATE;
    changed.bufferId = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed        = STATE;
    changed.modeId = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed              = STATE;
    changed.customModeId = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed              = STATE;
    changed.bufferFormat = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed              = STATE;
    changed.outputFormat = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                = STATE;
    changed.bufferModifier = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed         = STATE;
    changed.ctmHash = 20;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                 = STATE;
    changed.hdrMetadataHash = 20;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed              = STATE;
    changed.gammaLutHash = 20;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                = STATE;
    changed.degammaLutHash = 20;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed           = STATE;
    changed.modeWidth = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed            = STATE;
    changed.modeHeight = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                 = STATE;
    changed.modeRefreshRate = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                  = STATE;
    changed.presentationMode = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed           = STATE;
    changed.transform = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed             = STATE;
    changed.contentType = 10;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed         = STATE;
    changed.enabled = false;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed              = STATE;
    changed.adaptiveSync = false;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
    changed                = STATE;
    changed.wideColorGamut = false;
    EXPECT_FALSE(cache.canSkip(changed, false, false));
}

TEST(ScanoutTestCache, sameBufferCommitsPendingOutputState) {
    EXPECT_FALSE(sameBufferScanoutNeedsCommit(false, false, false));
    EXPECT_TRUE(sameBufferScanoutNeedsCommit(false, false, true));
    EXPECT_TRUE(sameBufferScanoutNeedsCommit(true, false, false));
    EXPECT_TRUE(sameBufferScanoutNeedsCommit(false, true, false));
}
