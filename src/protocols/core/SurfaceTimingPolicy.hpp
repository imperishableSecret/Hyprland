#pragma once

#include <cstddef>

namespace SurfaceTimingPolicy {
    enum class eWorkAction {
        DISCARD,
        SKIP,
        ROUTE,
    };

    eWorkAction workAction(bool discarded, bool hasPresentationFeedback, bool hasFIFOBarrier);
    bool        canUseSingleEnteredOutput(size_t enteredOutputCount, bool outputEligible);
    bool        candidatePreferred(double visibleArea, double bestVisibleArea, bool candidateWasPrevious);
}
