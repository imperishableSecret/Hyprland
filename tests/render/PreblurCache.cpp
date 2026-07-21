#include <render/PreblurCache.hpp>

#include <gtest/gtest.h>

using namespace Render;

TEST(PreblurCache, invalidUntilBuildCompletes) {
    CPreblurCacheState state;
    const auto         KEY = SPreblurCacheKey{.sourceDescriptionId = 10, .outputDescriptionId = 20};

    EXPECT_FALSE(state.validFor(KEY));
    state.markValid(KEY);
    EXPECT_TRUE(state.validFor(KEY));
}

TEST(PreblurCache, invalidationRejectsCompletedBuild) {
    CPreblurCacheState state;
    const auto         KEY = SPreblurCacheKey{.sourceDescriptionId = 10, .outputDescriptionId = 20};

    state.markValid(KEY);
    state.invalidate();
    EXPECT_FALSE(state.validFor(KEY));
}

TEST(PreblurCache, descriptionMismatchRejectsCompletedBuild) {
    CPreblurCacheState state;
    const auto         KEY = SPreblurCacheKey{.sourceDescriptionId = 10, .outputDescriptionId = 20};

    state.markValid(KEY);
    EXPECT_FALSE(state.validFor({.sourceDescriptionId = 11, .outputDescriptionId = 20}));
    EXPECT_FALSE(state.validFor({.sourceDescriptionId = 10, .outputDescriptionId = 21}));
}
