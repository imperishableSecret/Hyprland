#include "ScanoutKeepalive.hpp"

bool Monitor::scanoutKeepaliveDue(const SScanoutKeepaliveState& state) {
    if (!state.adaptiveSync || state.minimumRefreshHz <= 0.F || state.pendingPageFlip || state.pendingIdleFrame)
        return false;

    return state.elapsedMillis >= 1000.F / state.minimumRefreshHz;
}

Monitor::SScanoutCursorDecision Monitor::scanoutCursorDecision(bool suppressCursorCommit, const SScanoutKeepaliveState& state) {
    if (!suppressCursorCommit)
        return {};

    return {
        .skipCursorSchedule = true,
        .scheduleKeepalive  = scanoutKeepaliveDue(state),
    };
}
