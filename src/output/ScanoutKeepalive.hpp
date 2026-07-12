#pragma once

namespace Monitor {
    struct SScanoutKeepaliveState {
        bool  adaptiveSync     = false;
        float minimumRefreshHz = 0.F;
        float elapsedMillis    = 0.F;
        bool  pendingPageFlip  = false;
        bool  pendingIdleFrame = false;
    };

    struct SScanoutCursorDecision {
        bool skipCursorSchedule = false;
        bool scheduleKeepalive  = false;
    };

    bool                   scanoutKeepaliveDue(const SScanoutKeepaliveState& state);
    SScanoutCursorDecision scanoutCursorDecision(bool suppressCursorCommit, const SScanoutKeepaliveState& state);
}
