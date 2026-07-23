#include "CommitTimingReservation.hpp"

std::optional<Time::steady_tp> Monitor::nextPresentationSlot(const SFixedPresentationPhase& phase, const Time::steady_tp& notBefore) {
    if (phase.refresh <= Time::steady_dur::zero())
        return std::nullopt;

    auto slot = phase.presentation + phase.refresh;
    if (slot >= notBefore)
        return slot;

    const auto PERIODS = (notBefore - slot) / phase.refresh;
    slot += phase.refresh * PERIODS;
    if (slot < notBefore)
        slot += phase.refresh;

    return slot;
}

bool Monitor::presentationSlotDue(const SFixedPresentationPhase& phase, const Time::steady_tp& opportunityAt, const Time::steady_tp& target) {
    const auto OPPORTUNITY = nextPresentationSlot(phase, opportunityAt);
    const auto RESERVED    = nextPresentationSlot(phase, target);
    return OPPORTUNITY && RESERVED && *OPPORTUNITY >= *RESERVED;
}
