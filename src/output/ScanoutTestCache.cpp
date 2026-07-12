#include "ScanoutTestCache.hpp"

#include <aquamarine/output/Output.hpp>

using namespace Monitor;

bool CScanoutTestCache::canSkip(const SScanoutTestState& state, bool outputStateChanged, bool cursorStateChanged) const {
    return !outputStateChanged && !cursorStateChanged && m_acceptedState && *m_acceptedState == state;
}

bool CScanoutTestCache::canSkipNewBuffer(const SScanoutTestState& state) const {
    if (!m_acceptedState)
        return false;

    auto accepted     = *m_acceptedState;
    accepted.bufferId = state.bufferId;
    return accepted == state;
}

bool CScanoutTestCache::hasAcceptedState() const {
    return m_acceptedState.has_value();
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

bool Monitor::scanoutStateNeedsStructuralTest(uint32_t committed) {
    using Aquamarine::COutputState;

    constexpr uint32_t STRUCTURAL = COutputState::AQ_OUTPUT_STATE_ENABLED | COutputState::AQ_OUTPUT_STATE_ADAPTIVE_SYNC | COutputState::AQ_OUTPUT_STATE_PRESENTATION_MODE |
        COutputState::AQ_OUTPUT_STATE_GAMMA_LUT | COutputState::AQ_OUTPUT_STATE_MODE | COutputState::AQ_OUTPUT_STATE_FORMAT | COutputState::AQ_OUTPUT_STATE_CTM |
        COutputState::AQ_OUTPUT_STATE_HDR | COutputState::AQ_OUTPUT_STATE_DEGAMMA_LUT | COutputState::AQ_OUTPUT_STATE_WCG | COutputState::AQ_OUTPUT_STATE_CURSOR_SHAPE |
        COutputState::AQ_OUTPUT_STATE_CONTENT_TYPE;
    constexpr uint32_t NON_STRUCTURAL = COutputState::AQ_OUTPUT_STATE_DAMAGE | COutputState::AQ_OUTPUT_STATE_BUFFER | COutputState::AQ_OUTPUT_STATE_EXPLICIT_IN_FENCE |
        COutputState::AQ_OUTPUT_STATE_EXPLICIT_OUT_FENCE | COutputState::AQ_OUTPUT_STATE_CURSOR_POS;
    constexpr uint32_t KNOWN = STRUCTURAL | NON_STRUCTURAL;

    return (committed & STRUCTURAL) != 0 || (committed & ~KNOWN) != 0;
}
