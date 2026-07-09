#include "FrameCompletionDecision.hpp"

using namespace Monitor;

eFrameCompletionAction Monitor::frameCompletionAction(bool newSchedulingEnabled, bool fenceValid, bool pendingThird) {
    if (!newSchedulingEnabled)
        return FRAME_COMPLETION_IGNORE;
    if (fenceValid)
        return FRAME_COMPLETION_WAIT;
    return pendingThird ? FRAME_COMPLETION_FALLBACK_COMMIT_PENDING : FRAME_COMPLETION_FALLBACK;
}
