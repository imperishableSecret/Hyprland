#include "RenderTargetDecision.hpp"

using namespace Render::GL;

eRenderPassTarget Render::GL::renderPassTargetFor(const CRenderPassRequirements& requirements) {
    return requirements.requiresOffscreen() ? RENDER_PASS_TARGET_OFFSCREEN : RENDER_PASS_TARGET_OUTPUT;
}

bool Render::GL::renderPassTargetNeedsFullClear(eRenderPassTarget target) {
    return target == RENDER_PASS_TARGET_OFFSCREEN;
}
