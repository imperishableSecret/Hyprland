#pragma once

#include <cstdint>

namespace Render {
    bool layerNeedsPreblur(bool onTargetOutput, bool hasLayerSurface, int64_t xray);
}
