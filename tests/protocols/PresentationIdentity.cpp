#include <protocols/PresentationTime.hpp>

#include <gtest/gtest.h>
#include <limits>

class CPresentationProtocolTestAccess {
  public:
    static auto createState() {
        return makeUnique<CPresentationProtocol::SOutputPresentationState>();
    }

    static void stage(CPresentationProtocol::SOutputPresentationState& state) {
        auto& submission = state.submissions.emplace_back();
        submission.data.emplace_back(makeUnique<CQueuedPresentationData>(nullptr, std::vector<WP<CPresentationFeedback>>{}));
    }

    static uint64_t begin(CPresentationProtocol::SOutputPresentationState& state, bool bufferCommitted, bool zeroCopy) {
        return CPresentationProtocol::beginOutputCommit(state, bufferCommitted, zeroCopy);
    }

    static void finish(CPresentationProtocol::SOutputPresentationState& state, uint64_t id, bool success) {
        CPresentationProtocol::finishOutputCommit(state, id, success);
    }

    static bool staged(const CPresentationProtocol::SOutputPresentationState& state) {
        return CPresentationProtocol::hasStagedData(state);
    }

    static bool committing(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        const auto SUBMISSION = CPresentationProtocol::submissionFor(state, id);
        return SUBMISSION && SUBMISSION->state == CPresentationProtocol::SUBMISSION_COMMITTING;
    }

    static bool accepted(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        const auto SUBMISSION = CPresentationProtocol::submissionFor(state, id);
        return SUBMISSION && SUBMISSION->state == CPresentationProtocol::SUBMISSION_ACCEPTED;
    }

    static bool zeroCopy(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        const auto SUBMISSION = CPresentationProtocol::submissionFor(state, id);
        return SUBMISSION && SUBMISSION->zeroCopy;
    }

    static bool contains(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        return CPresentationProtocol::submissionFor(state, id);
    }

    static void erase(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        std::erase_if(state.submissions, [id](const auto& submission) { return submission.id == id; });
    }

    static void nextID(CPresentationProtocol::SOutputPresentationState& state, uint64_t id) {
        state.nextID = id;
    }
};

TEST(PresentationIdentity, allocatesOnlyForStagedBufferCommits) {
    auto state = CPresentationProtocolTestAccess::createState();

    EXPECT_EQ(CPresentationProtocolTestAccess::begin(*state, true, false), 0);
    CPresentationProtocolTestAccess::stage(*state);
    EXPECT_TRUE(CPresentationProtocolTestAccess::staged(*state));
    EXPECT_EQ(CPresentationProtocolTestAccess::begin(*state, false, false), 0);

    const uint64_t ID = CPresentationProtocolTestAccess::begin(*state, true, true);
    EXPECT_EQ(ID, 1);
    EXPECT_TRUE(CPresentationProtocolTestAccess::committing(*state, ID));
    EXPECT_TRUE(CPresentationProtocolTestAccess::zeroCopy(*state, ID));
}

TEST(PresentationIdentity, failedCommitReturnsSubmissionToStaged) {
    auto state = CPresentationProtocolTestAccess::createState();
    CPresentationProtocolTestAccess::stage(*state);

    const uint64_t FIRST_ID = CPresentationProtocolTestAccess::begin(*state, true, true);
    CPresentationProtocolTestAccess::finish(*state, FIRST_ID, false);
    EXPECT_TRUE(CPresentationProtocolTestAccess::staged(*state));
    EXPECT_FALSE(CPresentationProtocolTestAccess::contains(*state, FIRST_ID));

    const uint64_t RETRY_ID = CPresentationProtocolTestAccess::begin(*state, true, false);
    EXPECT_EQ(RETRY_ID, 2);
    EXPECT_FALSE(CPresentationProtocolTestAccess::zeroCopy(*state, RETRY_ID));
}

TEST(PresentationIdentity, matchesAcceptedSubmissionsByExactID) {
    auto state = CPresentationProtocolTestAccess::createState();
    CPresentationProtocolTestAccess::stage(*state);

    const uint64_t FIRST_ID = CPresentationProtocolTestAccess::begin(*state, true, false);
    CPresentationProtocolTestAccess::finish(*state, FIRST_ID, true);
    CPresentationProtocolTestAccess::stage(*state);
    const uint64_t SECOND_ID = CPresentationProtocolTestAccess::begin(*state, true, true);
    CPresentationProtocolTestAccess::finish(*state, SECOND_ID, true);

    EXPECT_TRUE(CPresentationProtocolTestAccess::accepted(*state, FIRST_ID));
    EXPECT_TRUE(CPresentationProtocolTestAccess::accepted(*state, SECOND_ID));
    EXPECT_FALSE(CPresentationProtocolTestAccess::contains(*state, 99));

    CPresentationProtocolTestAccess::erase(*state, SECOND_ID);
    EXPECT_TRUE(CPresentationProtocolTestAccess::accepted(*state, FIRST_ID));
    EXPECT_FALSE(CPresentationProtocolTestAccess::contains(*state, SECOND_ID));
}

TEST(PresentationIdentity, synchronousCompletionBeforeFinishIsHarmless) {
    auto state = CPresentationProtocolTestAccess::createState();
    CPresentationProtocolTestAccess::stage(*state);

    const uint64_t ID = CPresentationProtocolTestAccess::begin(*state, true, false);
    CPresentationProtocolTestAccess::erase(*state, ID);
    CPresentationProtocolTestAccess::finish(*state, ID, true);
    EXPECT_FALSE(CPresentationProtocolTestAccess::contains(*state, ID));
}

TEST(PresentationIdentity, wrapSkipsReservedZero) {
    auto state = CPresentationProtocolTestAccess::createState();
    CPresentationProtocolTestAccess::nextID(*state, std::numeric_limits<uint64_t>::max());
    CPresentationProtocolTestAccess::stage(*state);

    const uint64_t LAST_ID = CPresentationProtocolTestAccess::begin(*state, true, false);
    EXPECT_EQ(LAST_ID, std::numeric_limits<uint64_t>::max());
    CPresentationProtocolTestAccess::finish(*state, LAST_ID, true);
    CPresentationProtocolTestAccess::stage(*state);
    EXPECT_EQ(CPresentationProtocolTestAccess::begin(*state, true, false), 1);
}
