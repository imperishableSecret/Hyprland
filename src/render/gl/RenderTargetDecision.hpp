#pragma once

#include "../pass/PassRequirements.hpp"

#include <cstdint>

namespace Render::GL {
    enum eRenderPassTarget : uint8_t {
        RENDER_PASS_TARGET_OFFSCREEN = 0,
        RENDER_PASS_TARGET_OUTPUT,
    };

    eRenderPassTarget renderPassTargetFor(const CRenderPassRequirements& requirements);
    bool              renderPassTargetNeedsFullClear(eRenderPassTarget target);
}
