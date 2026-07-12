#include <output/ScanoutGPUCache.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

static SScanoutGPUIdentity validIdentity() {
    return {
        .allocator            = 1,
        .outputBackend        = 2,
        .compositorBackend    = 3,
        .compositorGeneration = 4,
        .allocatorFD          = 5,
        .compositorFD         = 6,
    };
}

TEST(ScanoutGPUCache, stableSingleGPUIdentityReusesResolvedTopology) {
    CScanoutGPUCache cache;
    const auto       identity = validIdentity();

    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
    cache.store(identity, {.allocatorDevice = 10, .compositorDevice = 10, .sameGPU = true});
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::SINGLE_GPU);
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::SINGLE_GPU);
}

TEST(ScanoutGPUCache, stableMultiGPUIdentityReusesResolvedTopology) {
    CScanoutGPUCache cache;
    const auto       identity = validIdentity();

    cache.store(identity, {.allocatorDevice = 10, .compositorDevice = 11, .sameGPU = false});
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::MULTI_GPU);
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::MULTI_GPU);
}

TEST(ScanoutGPUCache, backendOrFDReplacementRequiresResolution) {
    CScanoutGPUCache cache;
    const auto       original = validIdentity();
    cache.store(original, {.allocatorDevice = 10, .compositorDevice = 10, .sameGPU = true});

    auto identity = original;
    ++identity.outputBackend;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
    identity = original;
    ++identity.allocator;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
    identity = original;
    ++identity.compositorBackend;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
    identity = original;
    ++identity.allocatorFD;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
    identity = original;
    ++identity.compositorFD;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
}

TEST(ScanoutGPUCache, reusedFDWithNewGenerationRequiresResolution) {
    CScanoutGPUCache cache;
    auto             identity = validIdentity();
    cache.store(identity, {.allocatorDevice = 10, .compositorDevice = 10, .sameGPU = true});

    ++identity.compositorGeneration;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
}

TEST(ScanoutGPUCache, invalidIdentityIsConservativelyMultiGPU) {
    CScanoutGPUCache cache;
    auto             identity = validIdentity();

    identity.compositorBackend = 0;
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::MULTI_GPU);
    cache.store(identity, {.allocatorDevice = 10, .compositorDevice = 10, .sameGPU = true});
    EXPECT_EQ(cache.lookup(validIdentity()), eScanoutGPUCacheResult::UNKNOWN);
}

TEST(ScanoutGPUCache, missingGenerationIsConservativelyMultiGPU) {
    auto identity                 = validIdentity();
    identity.compositorGeneration = 0;
    CScanoutGPUCache cache;

    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::MULTI_GPU);
}

TEST(ScanoutGPUCache, invalidationForcesResolution) {
    CScanoutGPUCache cache;
    const auto       identity = validIdentity();
    cache.store(identity, {.allocatorDevice = 10, .compositorDevice = 10, .sameGPU = true});

    cache.invalidate();
    EXPECT_EQ(cache.lookup(identity), eScanoutGPUCacheResult::UNKNOWN);
}
