#include <output/MirrorDamageJournal.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

TEST(MirrorDamageJournal, snapshotsDamageThroughCurrentGeneration) {
    CMirrorDamageJournal journal;
    const CRegion        fullDamage{0, 0, 100, 100};

    journal.record(CRegion{0, 0, 10, 10});
    journal.record(CRegion{50, 50, 10, 10});

    const auto snapshot = journal.damageSince(0, fullDamage);
    EXPECT_EQ(snapshot.generation, 2u);
    EXPECT_TRUE(snapshot.damage.containsPoint({5, 5}));
    EXPECT_TRUE(snapshot.damage.containsPoint({55, 55}));
}

TEST(MirrorDamageJournal, consumersAdvanceIndependently) {
    CMirrorDamageJournal journal;
    const CRegion        fullDamage{0, 0, 100, 100};

    journal.record(CRegion{0, 0, 10, 10});
    const auto firstConsumer = journal.damageSince(0, fullDamage);

    journal.record(CRegion{50, 50, 10, 10});

    const auto advancedConsumer = journal.damageSince(firstConsumer.generation, fullDamage);
    const auto delayedConsumer  = journal.damageSince(0, fullDamage);

    EXPECT_FALSE(advancedConsumer.damage.containsPoint({5, 5}));
    EXPECT_TRUE(advancedConsumer.damage.containsPoint({55, 55}));
    EXPECT_TRUE(delayedConsumer.damage.containsPoint({5, 5}));
    EXPECT_TRUE(delayedConsumer.damage.containsPoint({55, 55}));
}

TEST(MirrorDamageJournal, historyOverflowFallsBackToFullDamage) {
    CMirrorDamageJournal journal{2};
    const CRegion        fullDamage{0, 0, 100, 100};

    journal.record(CRegion{0, 0, 10, 10});
    journal.record(CRegion{20, 20, 10, 10});
    journal.record(CRegion{40, 40, 10, 10});

    const auto staleSnapshot = journal.damageSince(0, fullDamage);
    EXPECT_EQ(staleSnapshot.damage.copy().getExtents(), fullDamage.copy().getExtents());
    EXPECT_TRUE(staleSnapshot.fullDamage);

    const auto retainedSnapshot = journal.damageSince(1, fullDamage);
    EXPECT_FALSE(retainedSnapshot.fullDamage);
    EXPECT_FALSE(retainedSnapshot.damage.containsPoint({5, 5}));
    EXPECT_TRUE(retainedSnapshot.damage.containsPoint({25, 25}));
    EXPECT_TRUE(retainedSnapshot.damage.containsPoint({45, 45}));
}

TEST(MirrorDamageJournal, emptyUpdatesDoNotAdvanceGeneration) {
    CMirrorDamageJournal journal;

    EXPECT_EQ(journal.record(CRegion{}), 0u);
    EXPECT_EQ(journal.generation(), 0u);
}
