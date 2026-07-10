#include <render/gl/RenderTargetDecision.hpp>

#include <gtest/gtest.h>

using namespace Render;
using namespace Render::GL;

TEST(RenderTargetDecision, selectsOutputForEligiblePass) {
    EXPECT_EQ(renderPassTargetFor({}), RENDER_PASS_TARGET_OUTPUT);
}

TEST(RenderTargetDecision, selectsOffscreenForEveryRequirement) {
    for (const auto REASON : {RPR_LIVE_BLUR, RPR_PRECOMPUTED_BLUR, RPR_WINDOW_TRANSFORMER, RPR_OUTPUT_COPY, RPR_SCREEN_SHADER, RPR_COLOR_CONVERSION, RPR_ZOOM, RPR_OUTPUT_TRANSFORM,
                              RPR_BACKEND_CONSTRAINT}) {
        CRenderPassRequirements requirements;
        requirements.add(REASON);

        EXPECT_EQ(renderPassTargetFor(requirements), RENDER_PASS_TARGET_OFFSCREEN);
    }
}

TEST(RenderTargetDecision, preservesBufferAgeContentsOnOutput) {
    EXPECT_FALSE(renderPassTargetNeedsFullClear(RENDER_PASS_TARGET_OUTPUT));
    EXPECT_TRUE(renderPassTargetNeedsFullClear(RENDER_PASS_TARGET_OFFSCREEN));
}
