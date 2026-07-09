#include <managers/screenshare/ShmReadbackDamage.hpp>

#include <gtest/gtest.h>

using namespace Screenshare;

TEST(ShmReadbackDamage, preservesCheapSparseDamage) {
    CRegion damage;
    damage.add(0, 0, 10, 10);
    damage.add(3800, 2000, 10, 10);

    const auto coalesced = coalesceShmReadbackDamage(damage, {3840, 2160});
    EXPECT_EQ(pixman_region32_n_rects(coalesced.pixman()), 2);
    EXPECT_TRUE(coalesced.containsPoint({5, 5}));
    EXPECT_TRUE(coalesced.containsPoint({3805, 2005}));
}

TEST(ShmReadbackDamage, coalescesClusteredRectanglesToExtents) {
    CRegion damage;
    for (int x = 0; x < 200; x += 20)
        damage.add(x, 0, 10, 10);

    const auto coalesced = coalesceShmReadbackDamage(damage, {1920, 1080});
    EXPECT_EQ(pixman_region32_n_rects(coalesced.pixman()), 1);
    EXPECT_EQ(coalesced.copy().getExtents(), CBox(0, 0, 190, 10));
}

TEST(ShmReadbackDamage, rowCostCanFavorOneLargerRead) {
    CRegion damage;
    for (int y = 0; y < 1080; y += 60)
        damage.add(0, y, 1920, 30);

    const auto coalesced = coalesceShmReadbackDamage(damage, {1920, 1080});
    EXPECT_EQ(pixman_region32_n_rects(coalesced.pixman()), 1);
}

TEST(ShmReadbackDamage, highlyFragmentedFullSpanFallsBackToFullRead) {
    CRegion damage;
    damage.add(0, 0, 1, 1);
    damage.add(1919, 1079, 1, 1);
    for (int y = 16; y < 1070; y += 16)
        damage.add((y * 37) % 1900, y, 1, 1);

    const auto coalesced = coalesceShmReadbackDamage(damage, {1920, 1080});
    EXPECT_EQ(pixman_region32_n_rects(coalesced.pixman()), 1);
    EXPECT_EQ(coalesced.copy().getExtents(), CBox(0, 0, 1920, 1080));
}
