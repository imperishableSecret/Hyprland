#pragma once

#include "../../helpers/math/Math.hpp"

namespace Screenshare {
    CRegion coalesceShmReadbackDamage(const CRegion& damage, const Vector2D& bufferSize);
}
