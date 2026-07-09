#pragma once

#include <cstdint>

namespace Monitor {
    enum eFrameCompletionAction : uint8_t {
        FRAME_COMPLETION_IGNORE = 0,
        FRAME_COMPLETION_WAIT,
        FRAME_COMPLETION_FALLBACK,
        FRAME_COMPLETION_FALLBACK_COMMIT_PENDING,
    };

    eFrameCompletionAction frameCompletionAction(bool newSchedulingEnabled, bool fenceValid, bool pendingThird);
}
