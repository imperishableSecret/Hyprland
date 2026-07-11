#include "FrameSubmission.hpp"

#include <utility>

using namespace Monitor;

uint64_t CFrameSubmissionLedger::nextID() {
    const auto ID = m_nextID++;
    if (m_nextID == 0)
        m_nextID = 1;
    return ID;
}

uint64_t CFrameSubmissionLedger::begin() {
    if (m_staged)
        abort();

    m_staged     = makeUnique<SSubmission>();
    m_staged->id = nextID();
    ++m_stats.staged;
    return m_staged->id;
}

void CFrameSubmissionLedger::attach(SP<IFrameSubmissionWork> work) {
    if (!work)
        return;

    if (!m_staged)
        begin();

    m_staged->work.emplace_back(std::move(work));
}

uint64_t CFrameSubmissionLedger::beginCommit(bool zeroCopy) {
    if (!m_staged)
        begin();

    m_staged->zeroCopy = zeroCopy;

    if (m_committing) {
        discard(*m_committing);
        m_committing.reset();
        m_presentationDuringCommit.reset();
        ++m_stats.aborted;
    }

    const auto ID = m_staged->id;
    m_committing  = std::move(m_staged);
    return ID;
}

uint64_t CFrameSubmissionLedger::finishCommit(bool success) {
    if (!m_committing)
        return 0;

    const auto ID = m_committing->id;
    if (!success) {
        m_staged = std::move(m_committing);
        m_presentationDuringCommit.reset();
        return ID;
    }

    // Aquamarine serializes buffer-bearing output commits. If another record
    // is still in flight after a new commit succeeds, that record can no
    // longer receive a matching presentation event. Keeping it would shift
    // every later protocol completion by one frame.
    for (auto& submission : m_inFlight) {
        discard(submission);
        ++m_stats.orphaned;
    }
    m_inFlight.clear();

    submit(*m_committing);
    ++m_stats.submitted;

    if (m_presentationDuringCommit) {
        completeSubmission(*m_committing, *m_presentationDuringCommit);
        m_presentationDuringCommit.reset();
        m_committing.reset();
        return ID;
    }

    m_inFlight.emplace_back(std::move(*m_committing));
    m_committing.reset();
    return ID;
}

void CFrameSubmissionLedger::abort() {
    if (!m_staged)
        return;

    discard(*m_staged);
    m_staged.reset();
    ++m_stats.aborted;
}

bool CFrameSubmissionLedger::complete(const SFramePresentation& event) {
    // Tearing and headless backends can emit presentation synchronously from
    // output.commit(). Defer protocol completion until the commit result is
    // known instead of orphaning the event or consuming an older record.
    if (m_committing) {
        if (m_presentationDuringCommit) {
            ++m_stats.orphaned;
            return false;
        }

        m_presentationDuringCommit = event;
        return true;
    }

    if (m_inFlight.empty()) {
        ++m_stats.orphaned;
        return false;
    }

    auto submission = std::move(m_inFlight.front());
    m_inFlight.pop_front();

    completeSubmission(submission, event);
    return true;
}

void CFrameSubmissionLedger::discardAll() {
    abort();
    if (m_committing) {
        discard(*m_committing);
        m_committing.reset();
    }
    m_presentationDuringCommit.reset();
    for (auto& submission : m_inFlight)
        discard(submission);
    m_inFlight.clear();
}

bool CFrameSubmissionLedger::hasStagedSubmission() const {
    return sc<bool>(m_staged);
}

bool CFrameSubmissionLedger::hasStagedWork() const {
    return m_staged && !m_staged->work.empty();
}

size_t CFrameSubmissionLedger::stagedWorkCount() const {
    return m_staged ? m_staged->work.size() : 0;
}

size_t CFrameSubmissionLedger::inFlightCount() const {
    return m_inFlight.size();
}

uint64_t CFrameSubmissionLedger::stagedID() const {
    return m_staged ? m_staged->id : 0;
}

uint64_t CFrameSubmissionLedger::oldestInFlightID() const {
    return m_inFlight.empty() ? 0 : m_inFlight.front().id;
}

uint64_t CFrameSubmissionLedger::presentationTargetID() const {
    return m_committing ? m_committing->id : oldestInFlightID();
}

CFrameSubmissionLedger::SStats CFrameSubmissionLedger::stats() const {
    return m_stats;
}

void CFrameSubmissionLedger::submit(SSubmission& submission) {
    for (const auto& work : submission.work) {
        if (work)
            work->submitted();
    }
}

void CFrameSubmissionLedger::discard(SSubmission& submission) {
    for (const auto& work : submission.work) {
        if (work)
            work->discarded();
    }
    ++m_stats.discarded;
}

void CFrameSubmissionLedger::completeSubmission(SSubmission& submission, const SFramePresentation& event) {
    if (!event.presented) {
        discard(submission);
        return;
    }

    auto submissionEvent     = event;
    submissionEvent.zeroCopy = submission.zeroCopy;
    for (const auto& work : submission.work) {
        if (work)
            work->presented(submissionEvent);
    }

    ++m_stats.presented;
}
