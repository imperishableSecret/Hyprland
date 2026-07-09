#include "PreblurDecision.hpp"

bool Render::layerNeedsPreblur(bool onTargetOutput, bool hasLayerSurface, int64_t xray) {
    return onTargetOutput && hasLayerSurface && xray == 1;
}
