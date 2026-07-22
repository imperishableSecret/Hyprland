#include <protocols/Fifo.hpp>
#include <protocols/types/ContentUpdate.hpp>
#include <protocols/types/SurfaceState.hpp>

#include <gtest/gtest.h>
#include <limits>

class CFifoBarrierConditionTestAccess {
  public:
    static void nextEpoch(CFifoBarrierCondition& condition, uint64_t epoch) {
        condition.m_nextEpoch = epoch;
    }
};

TEST(Fifo, requestsRemainOnTheirContentUpdates) {
    SSurfaceState pending;
    pending.barrierSet = true;

    const SSurfaceState barrier = pending;
    pending.reset();
    pending.waitBarrier = true;

    const SSurfaceState wait = pending;
    pending.reset();

    EXPECT_TRUE(barrier.barrierSet);
    EXPECT_FALSE(barrier.waitBarrier);
    EXPECT_FALSE(wait.barrierSet);
    EXPECT_TRUE(wait.waitBarrier);
    EXPECT_FALSE(pending.barrierSet);
    EXPECT_FALSE(pending.waitBarrier);
}

TEST(Fifo, setAndWaitCanShareOneContentUpdate) {
    SSurfaceState state;
    state.barrierSet  = true;
    state.waitBarrier = true;

    EXPECT_TRUE(state.barrierSet);
    EXPECT_TRUE(state.waitBarrier);
}

TEST(Fifo, conditionClearsOnlyForItsActiveEpoch) {
    CFifoBarrierCondition condition;
    const uint64_t        FIRST  = condition.reserveEpoch();
    const uint64_t        SECOND = condition.reserveEpoch();

    condition.activate(FIRST);
    EXPECT_TRUE(condition.matches(FIRST));
    EXPECT_FALSE(condition.clear(SECOND));
    EXPECT_TRUE(condition.matches(FIRST));
    EXPECT_TRUE(condition.clear(FIRST));
    EXPECT_EQ(condition.activeEpoch(), 0);
}

TEST(Fifo, epochAllocationSkipsReservedZeroOnWrap) {
    CFifoBarrierCondition condition;
    CFifoBarrierConditionTestAccess::nextEpoch(condition, std::numeric_limits<uint64_t>::max());

    EXPECT_EQ(condition.reserveEpoch(), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(condition.reserveEpoch(), 1);
}

TEST(Fifo, queueReportsNewestBarrierEpoch) {
    CContentUpdateQueue queue;

    SSurfaceState       first;
    first.fifoBarrierEpoch = 3;
    queue.enqueue(makeUnique<CContentUpdate>(first, WP<CWLSurfaceResource>{}));

    SSurfaceState second;
    second.fifoBarrierEpoch = 5;
    queue.enqueue(makeUnique<CContentUpdate>(second, WP<CWLSurfaceResource>{}));

    EXPECT_EQ(queue.latestFifoBarrierEpoch(), 5);
}
