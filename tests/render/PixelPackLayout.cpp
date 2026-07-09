#include <render/gl/PixelPackLayout.hpp>

#include <gtest/gtest.h>

using namespace Render::GL;

TEST(PixelPackLayout, mapsPartialRectIntoStridedBuffer) {
    const auto layout = calculatePixelPackLayout(40000, 400, 40, 80, 4, 10, 20, 20, 5);

    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->direct);
    EXPECT_EQ(layout->destinationOffset, 8040u);
    EXPECT_EQ(layout->rowLength, 100u);
    EXPECT_EQ(layout->skipPixels, 10u);
    EXPECT_EQ(layout->skipRows, 20u);
}

TEST(PixelPackLayout, fullRowsUseOneDirectTransfer) {
    const auto layout = calculatePixelPackLayout(40000, 400, 0, 400, 4, 0, 0, 100, 100);

    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(layout->direct);
    EXPECT_EQ(layout->rowLength, 100u);
    EXPECT_EQ(layout->scratchBytes, 40000u);
}

TEST(PixelPackLayout, nonPixelAlignedStrideUsesScratchBuffer) {
    const auto layout = calculatePixelPackLayout(40200, 402, 40, 80, 4, 10, 20, 20, 5);

    ASSERT_TRUE(layout.has_value());
    EXPECT_FALSE(layout->direct);
    EXPECT_EQ(layout->destinationOffset, 8080u);
    EXPECT_EQ(layout->scratchBytes, 400u);
}

TEST(PixelPackLayout, rejectsOutOfBoundsDestination) {
    EXPECT_FALSE(calculatePixelPackLayout(1000, 400, 40, 80, 4, 10, 20, 20, 5).has_value());
}

TEST(PixelPackLayout, rejectsDimensionsOutsideGLRange) {
    EXPECT_FALSE(calculatePixelPackLayout(40000, 400, 0, 400, 4, 0, 0, 100, UINT32_MAX).has_value());
}
