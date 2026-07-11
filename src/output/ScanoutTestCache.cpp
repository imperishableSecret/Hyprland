#include "ScanoutTestCache.hpp"

using namespace Monitor;

bool CScanoutTestCache::canSkip(const SScanoutTestState& state, bool outputStateChanged, bool cursorStateChanged) const {
    return !outputStateChanged && !cursorStateChanged && m_acceptedState && *m_acceptedState == state;
}

void CScanoutTestCache::accept(const SScanoutTestState& state) {
    m_acceptedState = state;
}

void CScanoutTestCache::invalidate() {
    m_acceptedState.reset();
}

bool Monitor::sameBufferScanoutNeedsCommit(bool cursorCommitDue, bool vrrKeepaliveDue, bool outputStateCommitDue, bool protocolCompletionDue) {
    return cursorCommitDue || vrrKeepaliveDue || outputStateCommitDue || protocolCompletionDue;
}
