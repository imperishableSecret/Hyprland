#pragma once

#include "../helpers/time/Time.hpp"

#include <optional>

namespace Monitor {
    struct SFixedPresentationPhase {
        Time::steady_tp  presentation;
        Time::steady_dur refresh;
    };

    std::optional<Time::steady_tp> nextPresentationSlot(const SFixedPresentationPhase& phase, const Time::steady_tp& notBefore);
    bool                           presentationSlotDue(const SFixedPresentationPhase& phase, const Time::steady_tp& opportunityAt, const Time::steady_tp& target);
}
