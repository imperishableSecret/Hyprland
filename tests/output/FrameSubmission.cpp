#include <output/FrameSubmission.hpp>

#include <gtest/gtest.h>

using namespace Monitor;

class CTestSubmissionWork : public IFrameSubmissionWork {
  public:
    virtual void submitted() override {
        ++submittedCount;
    }

    virtual void presented(const SFramePresentation& event) override {
        ++presentedCount;
        lastSequence = event.sequence;
        zeroCopy     = event.zeroCopy;
    }

    virtual void discarded() override {
        ++discardedCount;
    }

    int      submittedCount = 0;
    int      presentedCount = 0;
    int      discardedCount = 0;
    uint64_t lastSequence   = 0;
    bool     zeroCopy       = false;
};

TEST(FrameSubmission, successfulCommitCompletesOnlyItsOwnWork) {
    CFrameSubmissionLedger ledger;
    const auto             FIRST  = makeShared<CTestSubmissionWork>();
    const auto             SECOND = makeShared<CTestSubmissionWork>();

    ledger.begin();
    ledger.attach(FIRST);
    EXPECT_TRUE(ledger.beginCommit(false));
    EXPECT_TRUE(ledger.finishCommit(true));
    EXPECT_EQ(FIRST->submittedCount, 1);

    EXPECT_TRUE(ledger.complete({.presented = true, .sequence = 10}));
    EXPECT_EQ(FIRST->presentedCount, 1);
    EXPECT_FALSE(FIRST->zeroCopy);

    ledger.begin();
    ledger.attach(SECOND);
    EXPECT_TRUE(ledger.beginCommit(true));
    EXPECT_TRUE(ledger.finishCommit(true));
    EXPECT_EQ(SECOND->submittedCount, 1);

    EXPECT_TRUE(ledger.complete({.presented = true, .sequence = 11}));
    EXPECT_EQ(SECOND->presentedCount, 1);
    EXPECT_TRUE(SECOND->zeroCopy);
}

TEST(FrameSubmission, abortedSpeculativeFrameNeverBecomesInflight) {
    CFrameSubmissionLedger ledger;
    const auto             WORK = makeShared<CTestSubmissionWork>();
    ledger.begin();
    ledger.attach(WORK);
    ledger.abort();

    EXPECT_EQ(WORK->discardedCount, 1);
    EXPECT_EQ(ledger.inFlightCount(), 0);
    EXPECT_FALSE(ledger.complete({.presented = true}));
}

TEST(FrameSubmission, precedingPresentationCannotConsumeRenderAheadWork) {
    CFrameSubmissionLedger ledger;
    const auto             PRESENTED = makeShared<CTestSubmissionWork>();
    const auto             AHEAD     = makeShared<CTestSubmissionWork>();

    ledger.begin();
    ledger.attach(PRESENTED);
    ledger.beginCommit(false);
    ledger.finishCommit(true);
    ledger.begin();
    ledger.attach(AHEAD);

    EXPECT_TRUE(ledger.complete({.presented = true, .sequence = 7}));
    EXPECT_EQ(PRESENTED->presentedCount, 1);
    EXPECT_EQ(AHEAD->presentedCount, 0);
    EXPECT_EQ(AHEAD->discardedCount, 0);
    EXPECT_TRUE(ledger.hasStagedWork());
}

TEST(FrameSubmission, notPresentedDiscardsMatchingSubmission) {
    CFrameSubmissionLedger ledger;
    const auto             WORK = makeShared<CTestSubmissionWork>();
    ledger.begin();
    ledger.attach(WORK);
    ledger.beginCommit(false);
    ledger.finishCommit(true);

    EXPECT_EQ(WORK->submittedCount, 1);
    EXPECT_TRUE(ledger.complete({.presented = false}));
    EXPECT_EQ(WORK->presentedCount, 0);
    EXPECT_EQ(WORK->discardedCount, 1);
}

TEST(FrameSubmission, duplicatePresentationCannotCompleteTwice) {
    CFrameSubmissionLedger ledger;
    const auto             WORK = makeShared<CTestSubmissionWork>();
    ledger.begin();
    ledger.attach(WORK);
    ledger.beginCommit(false);
    ledger.finishCommit(true);

    EXPECT_TRUE(ledger.complete({.presented = true}));
    EXPECT_FALSE(ledger.complete({.presented = true}));
    EXPECT_EQ(WORK->presentedCount, 1);
}

TEST(FrameSubmission, teardownDiscardsStagedAndInflightWork) {
    CFrameSubmissionLedger ledger;
    const auto             IN_FLIGHT = makeShared<CTestSubmissionWork>();
    const auto             STAGED    = makeShared<CTestSubmissionWork>();
    ledger.begin();
    ledger.attach(IN_FLIGHT);
    ledger.beginCommit(false);
    ledger.finishCommit(true);
    ledger.begin();
    ledger.attach(STAGED);

    ledger.discardAll();
    EXPECT_EQ(IN_FLIGHT->discardedCount, 1);
    EXPECT_EQ(STAGED->discardedCount, 1);
    EXPECT_EQ(ledger.inFlightCount(), 0);
    EXPECT_FALSE(ledger.hasStagedSubmission());
}

TEST(FrameSubmission, synchronousPresentationCompletesCommittingSubmission) {
    CFrameSubmissionLedger ledger;
    const auto             WORK = makeShared<CTestSubmissionWork>();

    ledger.begin();
    ledger.attach(WORK);
    const auto ID = ledger.beginCommit(true);

    EXPECT_EQ(ledger.presentationTargetID(), ID);
    EXPECT_TRUE(ledger.complete({.presented = true, .sequence = 12}));
    EXPECT_EQ(WORK->presentedCount, 0);
    EXPECT_EQ(ledger.inFlightCount(), 0);

    EXPECT_EQ(ledger.finishCommit(true), ID);
    EXPECT_EQ(WORK->presentedCount, 1);
    EXPECT_EQ(WORK->lastSequence, 12);
    EXPECT_TRUE(WORK->zeroCopy);
    EXPECT_EQ(ledger.inFlightCount(), 0);
}

TEST(FrameSubmission, failedCommitRestoresStagedWorkForRetry) {
    CFrameSubmissionLedger ledger;
    const auto             WORK = makeShared<CTestSubmissionWork>();

    ledger.begin();
    ledger.attach(WORK);
    const auto ID = ledger.beginCommit(false);
    EXPECT_EQ(ledger.finishCommit(false), ID);
    EXPECT_TRUE(ledger.hasStagedWork());
    EXPECT_EQ(WORK->submittedCount, 0);
    EXPECT_EQ(WORK->discardedCount, 0);

    EXPECT_EQ(ledger.beginCommit(false), ID);
    EXPECT_EQ(ledger.finishCommit(true), ID);
    EXPECT_EQ(WORK->submittedCount, 1);
    EXPECT_TRUE(ledger.complete({.presented = true}));
    EXPECT_EQ(WORK->presentedCount, 1);
}

TEST(FrameSubmission, successfulCommitDiscardsStaleInflightSubmission) {
    CFrameSubmissionLedger ledger;
    const auto             STALE   = makeShared<CTestSubmissionWork>();
    const auto             CURRENT = makeShared<CTestSubmissionWork>();

    ledger.begin();
    ledger.attach(STALE);
    ledger.beginCommit(false);
    ledger.finishCommit(true);

    ledger.begin();
    ledger.attach(CURRENT);
    ledger.beginCommit(false);
    ledger.finishCommit(true);

    EXPECT_EQ(STALE->discardedCount, 1);
    EXPECT_EQ(ledger.inFlightCount(), 1);
    EXPECT_TRUE(ledger.complete({.presented = true, .sequence = 13}));
    EXPECT_EQ(CURRENT->presentedCount, 1);
    EXPECT_EQ(CURRENT->lastSequence, 13);
}
