#include <render/PreblurDecision.hpp>

#include <gtest/gtest.h>

TEST(PreblurDecision, acceptsXrayLayerOnTargetOutput) {
    EXPECT_TRUE(Render::layerNeedsPreblur(true, true, 1));
}

TEST(PreblurDecision, rejectsXrayLayerOnAnotherOutput) {
    EXPECT_FALSE(Render::layerNeedsPreblur(false, true, 1));
}

TEST(PreblurDecision, rejectsDestroyedOrDetachedLayers) {
    EXPECT_FALSE(Render::layerNeedsPreblur(true, false, 1));
    EXPECT_FALSE(Render::layerNeedsPreblur(false, true, 1));
}

TEST(PreblurDecision, requiresExplicitXrayRule) {
    EXPECT_FALSE(Render::layerNeedsPreblur(true, true, 0));
    EXPECT_FALSE(Render::layerNeedsPreblur(true, true, -1));
}
