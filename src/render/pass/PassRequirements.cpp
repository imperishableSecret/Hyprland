#include "PassRequirements.hpp"

using namespace Render;

void CRenderPassRequirements::add(eRenderPassRequirement requirement) {
    m_reasons |= requirement;
}

void CRenderPassRequirements::add(const SRenderPassExternalRequirements& requirements) {
    if (requirements.outputCopy)
        add(RPR_OUTPUT_COPY);
    if (requirements.screenShader)
        add(RPR_SCREEN_SHADER);
    if (requirements.colorConversion)
        add(RPR_COLOR_CONVERSION);
    if (requirements.zoom)
        add(RPR_ZOOM);
    if (requirements.outputTransform)
        add(RPR_OUTPUT_TRANSFORM);
    if (requirements.backendConstraint)
        add(RPR_BACKEND_CONSTRAINT);
}

bool CRenderPassRequirements::has(eRenderPassRequirement requirement) const {
    return m_reasons & requirement;
}

bool CRenderPassRequirements::requiresOffscreen() const {
    return m_reasons != RPR_NONE;
}

uint32_t CRenderPassRequirements::reasons() const {
    return m_reasons;
}
