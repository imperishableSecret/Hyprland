#include <render/PreblurCache.hpp>

#include <gtest/gtest.h>

using namespace Render;

TEST(PreblurCache, invalidUntilMatchingBuildCompletes) {
    CPreblurCacheState state;
    const auto         SDR = SPreblurCacheKey{.generation = 1, .sourceDescriptionId = 10, .outputDescriptionId = 20};

    EXPECT_FALSE(state.validFor(SDR));
    EXPECT_TRUE(state.markValid(SDR, 1));
    EXPECT_TRUE(state.validFor(SDR));
}

TEST(PreblurCache, colorGenerationChangeRejectsPreviousCache) {
    CPreblurCacheState state;
    const auto         HDR = SPreblurCacheKey{.generation = 7, .sourceDescriptionId = 21, .outputDescriptionId = 31};
    const auto         SDR = SPreblurCacheKey{.generation = 8, .sourceDescriptionId = 20, .outputDescriptionId = 30};

    ASSERT_TRUE(state.markValid(HDR, 7));
    EXPECT_FALSE(state.validFor(SDR));

    state.invalidate();
    EXPECT_FALSE(state.validFor(HDR));
}

TEST(PreblurCache, descriptionMismatchRejectsCacheWithinGeneration) {
    CPreblurCacheState state;
    const auto         ORIGINAL = SPreblurCacheKey{.generation = 3, .sourceDescriptionId = 10, .outputDescriptionId = 20};

    ASSERT_TRUE(state.markValid(ORIGINAL, 3));
    EXPECT_FALSE(state.validFor({.generation = 3, .sourceDescriptionId = 11, .outputDescriptionId = 20}));
    EXPECT_FALSE(state.validFor({.generation = 3, .sourceDescriptionId = 10, .outputDescriptionId = 21}));
}

TEST(PreblurCache, staleCompletionCannotValidateNewGeneration) {
    CPreblurCacheState state;
    const auto         STALE = SPreblurCacheKey{.generation = 4, .sourceDescriptionId = 10, .outputDescriptionId = 20};

    EXPECT_FALSE(state.markValid(STALE, 5));
    EXPECT_FALSE(state.validFor(STALE));
}

TEST(PreblurCache, rebuildsUntilTransitionFramesAreComplete) {
    EXPECT_TRUE(preblurNeedsRebuild(true, 2));
    EXPECT_TRUE(preblurNeedsRebuild(true, 1));
    EXPECT_FALSE(preblurNeedsRebuild(true, 0));
    EXPECT_TRUE(preblurNeedsRebuild(false, 0));
}
