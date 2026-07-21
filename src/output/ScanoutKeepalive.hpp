#pragma once

namespace Monitor {
    struct SScanoutKeepaliveState {
        bool  adaptiveSync     = false;
        float minimumRefreshHz = 0.F;
        float elapsedMillis    = 0.F;
        bool  pendingPageFlip  = false;
        bool  pendingIdleFrame = false;
    };

    bool scanoutKeepaliveDue(const SScanoutKeepaliveState& state);
}
