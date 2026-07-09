#include <managers/screenshare/ScreenshareDamage.hpp>

#include <gtest/gtest.h>

using namespace Monitor;
using namespace Screenshare;

TEST(ScreenshareDamage, retainsDamageArrivingAfterSnapshot) {
    CMirrorDamageJournal journal;
    const CRegion        fullDamage{0, 0, 100, 100};

    journal.record(CRegion{0, 0, 10, 10});
    auto frozen = freezeCaptureDamage(journal.damageSince(0, fullDamage), {100, 100}, {100, 100}, WL_OUTPUT_TRANSFORM_NORMAL, false);

    journal.record(CRegion{50, 50, 10, 10});

    EXPECT_TRUE(frozen.bufferDamage.containsPoint({5, 5}));
    EXPECT_FALSE(frozen.bufferDamage.containsPoint({55, 55}));
    EXPECT_EQ(consumedCaptureGeneration(frozen.full, frozen.generation, journal.generation()), 1u);

    const auto next = journal.damageSince(frozen.generation, fullDamage);
    EXPECT_FALSE(next.damage.containsPoint({5, 5}));
    EXPECT_TRUE(next.damage.containsPoint({55, 55}));
}

TEST(ScreenshareDamage, consumersFreezeIndependentSnapshots) {
    CMirrorDamageJournal journal;
    const CRegion        fullDamage{0, 0, 100, 100};

    journal.record(CRegion{0, 0, 10, 10});
    auto first = freezeCaptureDamage(journal.damageSince(0, fullDamage), {100, 100}, {100, 100}, WL_OUTPUT_TRANSFORM_NORMAL, false);

    journal.record(CRegion{50, 50, 10, 10});
    auto advanced = freezeCaptureDamage(journal.damageSince(first.generation, fullDamage), {100, 100}, {100, 100}, WL_OUTPUT_TRANSFORM_NORMAL, false);
    auto delayed  = freezeCaptureDamage(journal.damageSince(0, fullDamage), {100, 100}, {100, 100}, WL_OUTPUT_TRANSFORM_NORMAL, false);

    EXPECT_FALSE(advanced.bufferDamage.containsPoint({5, 5}));
    EXPECT_TRUE(advanced.bufferDamage.containsPoint({55, 55}));
    EXPECT_TRUE(delayed.bufferDamage.containsPoint({5, 5}));
    EXPECT_TRUE(delayed.bufferDamage.containsPoint({55, 55}));
}

TEST(ScreenshareDamage, transformsRotatedOutputDamageToBufferCoordinates) {
    SMirrorDamageSnapshot snapshot{
        .generation = 7,
        .damage     = CRegion{100, 200, 30, 40},
    };

    const auto frozen = freezeCaptureDamage(std::move(snapshot), {1920, 1080}, {1080, 1920}, WL_OUTPUT_TRANSFORM_90, false);

    EXPECT_EQ(frozen.generation, 7u);
    EXPECT_EQ(frozen.monitorDamage.copy().getExtents(), CBox(100, 200, 30, 40));
    EXPECT_EQ(frozen.bufferDamage.copy().getExtents(), CBox(200, 950, 40, 30));
}

TEST(ScreenshareDamage, firstCursorRegionAndWindowCapturesUseFullDamage) {
    EXPECT_TRUE(captureNeedsFullDamage(true, false, true));
    EXPECT_TRUE(captureNeedsFullDamage(false, true, true));
    EXPECT_TRUE(captureNeedsFullDamage(false, false, false));
    EXPECT_FALSE(captureNeedsFullDamage(false, false, true));

    const auto frozen = freezeCaptureDamage({.generation = 4}, {1920, 1080}, {1080, 1920}, WL_OUTPUT_TRANSFORM_90, true);
    EXPECT_TRUE(frozen.full);
    EXPECT_EQ(frozen.bufferDamage.copy().getExtents(), CBox(0, 0, 1920, 1080));
    EXPECT_EQ(frozen.monitorDamage.copy().getExtents(), CBox(0, 0, 1080, 1920));
    EXPECT_EQ(consumedCaptureGeneration(frozen.full, frozen.generation, 9), 9u);
}

TEST(ScreenshareDamage, journalFallbackConsumesAtCopyGeneration) {
    auto frozen = freezeCaptureDamage({.generation = 4, .damage = CRegion{0, 0, 100, 100}, .fullDamage = true}, {100, 100}, {100, 100}, WL_OUTPUT_TRANSFORM_NORMAL, false);

    EXPECT_TRUE(frozen.full);
    EXPECT_EQ(consumedCaptureGeneration(frozen.full, frozen.generation, 9), 9u);
}
