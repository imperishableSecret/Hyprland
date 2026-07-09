#include "AnimationTickPacer.hpp"

#include <algorithm>
#include <cmath>

using namespace Animation;

static constexpr float DEFAULT_REFRESH_RATE = 60.F;
static constexpr float MIN_REFRESH_RATE     = 30.F;
static constexpr float MAX_REFRESH_RATE     = 1000.F;

void                   CAnimationTickPacer::considerOutput(float refreshRate, bool enabled) {
    if (!enabled || !std::isfinite(refreshRate) || refreshRate <= 0.F)
        return;

    m_fastestRefreshRate = std::max(m_fastestRefreshRate, refreshRate);
}

std::chrono::microseconds CAnimationTickPacer::interval() const {
    const auto REFRESH_RATE = std::clamp(m_fastestRefreshRate > 0.F ? m_fastestRefreshRate : DEFAULT_REFRESH_RATE, MIN_REFRESH_RATE, MAX_REFRESH_RATE);
    return std::chrono::microseconds{static_cast<int64_t>(std::ceil(1'000'000.F / REFRESH_RATE))};
}
