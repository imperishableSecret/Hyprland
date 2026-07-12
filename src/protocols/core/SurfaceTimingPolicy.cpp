#include "SurfaceTimingPolicy.hpp"

SurfaceTimingPolicy::eWorkAction SurfaceTimingPolicy::workAction(bool discarded, bool hasPresentationFeedback, bool hasFIFOBarrier) {
    if (discarded)
        return eWorkAction::DISCARD;
    if (!hasPresentationFeedback && !hasFIFOBarrier)
        return eWorkAction::SKIP;
    return eWorkAction::ROUTE;
}

bool SurfaceTimingPolicy::canUseSingleEnteredOutput(size_t enteredOutputCount, bool outputEligible) {
    return enteredOutputCount == 1 && outputEligible;
}

bool SurfaceTimingPolicy::candidatePreferred(double visibleArea, double bestVisibleArea, bool candidateWasPrevious) {
    return visibleArea > bestVisibleArea || (visibleArea == bestVisibleArea && candidateWasPrevious);
}
