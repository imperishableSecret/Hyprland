#include <protocols/types/ContentUpdate.hpp>

#include <gtest/gtest.h>

class CContentUpdateTestAccess {
  public:
    static void applyState(CContentUpdate& update) {
        update.applyState();
    }

    static WP<CContentUpdate> previous(const CContentUpdate& update) {
        return update.m_previous;
    }

    static size_t dependencyCount(const CContentUpdate& update) {
        return update.m_dependencies.size();
    }

    static WP<CContentUpdate> claimedBy(const CContentUpdate& update) {
        return update.m_claimedBy;
    }

    static bool isCandidate(const CContentUpdateQueue& queue, const WP<CContentUpdate>& update) {
        return queue.isCandidate(update);
    }

    static bool reachableFromDesynchronized(const CContentUpdateQueue& queue, const WP<CContentUpdate>& update) {
        std::vector<WP<CContentUpdate>> visiting;
        return queue.reachableFromDesynchronized(update, visiting);
    }
};

TEST(ContentUpdates, extensionStateIsCapturedPerUpdate) {
    SSurfaceState  state;
    CContentUpdate first{state, {}};
    CContentUpdate second{state, {}};
    int            pending = 1;
    int            active  = 0;

    first.addActivation([&active, value = pending] { active = value; });
    pending = 2;
    second.addActivation([&active, value = pending] { active = value; });

    CContentUpdateTestAccess::applyState(first);
    EXPECT_EQ(active, 1);

    CContentUpdateTestAccess::applyState(second);
    EXPECT_EQ(active, 2);
}

TEST(ContentUpdates, activationsRunInRegistrationOrder) {
    SSurfaceState  state;
    CContentUpdate update{state, {}};
    int            active = 1;

    update.addActivation([&active] { active += 2; });
    update.addActivation([&active] { active *= 3; });

    CContentUpdateTestAccess::applyState(update);
    EXPECT_EQ(active, 9);
}

TEST(ContentUpdates, updatesRetainSameSurfaceReceiveOrder) {
    SSurfaceState       state;
    CContentUpdateQueue queue;
    const auto          FIRST  = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}));
    const auto          SECOND = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}));

    EXPECT_EQ(CContentUpdateTestAccess::previous(*SECOND), FIRST);
}

TEST(ContentUpdates, onlyLeadingDesynchronizedUpdatesAreCandidates) {
    SSurfaceState       state;
    CContentUpdateQueue queue;
    const auto          FIRST   = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::DESYNCHRONIZED));
    const auto          SECOND  = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::DESYNCHRONIZED));
    const auto          SYNC    = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::SYNCHRONIZED));
    const auto          BLOCKED = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::DESYNCHRONIZED));

    EXPECT_TRUE(CContentUpdateTestAccess::isCandidate(queue, FIRST));
    EXPECT_TRUE(CContentUpdateTestAccess::isCandidate(queue, SECOND));
    EXPECT_FALSE(CContentUpdateTestAccess::isCandidate(queue, SYNC));
    EXPECT_FALSE(CContentUpdateTestAccess::isCandidate(queue, BLOCKED));
}

TEST(ContentUpdates, synchronizedChildDependenciesHaveBackReferences) {
    SSurfaceState       state;
    CContentUpdateQueue childQueue;
    CContentUpdateQueue parentQueue;
    const auto          CHILD  = childQueue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::SYNCHRONIZED));
    const auto          PARENT = parentQueue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::DESYNCHRONIZED));

    childQueue.claimNewestSynchronized(PARENT);

    EXPECT_EQ(CContentUpdateTestAccess::dependencyCount(*PARENT), 1U);
    EXPECT_EQ(CContentUpdateTestAccess::claimedBy(*CHILD), PARENT);
}

TEST(ContentUpdates, synchronizedUpdateReachabilityFollowsSameSurfaceOrder) {
    SSurfaceState       state;
    CContentUpdateQueue queue;
    const auto          SYNC = queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::SYNCHRONIZED));
    queue.enqueue(makeUnique<CContentUpdate>(state, WP<CWLSurfaceResource>{}, eContentUpdateMode::DESYNCHRONIZED));

    EXPECT_TRUE(CContentUpdateTestAccess::reachableFromDesynchronized(queue, SYNC));
}
