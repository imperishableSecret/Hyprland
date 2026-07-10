#pragma once

#include <cstdint>

namespace Render {
    enum eRenderPassRequirement : uint32_t {
        RPR_NONE               = 0,
        RPR_LIVE_BLUR          = 1 << 0,
        RPR_PRECOMPUTED_BLUR   = 1 << 1,
        RPR_WINDOW_TRANSFORMER = 1 << 2,
        RPR_OUTPUT_COPY        = 1 << 3,
        RPR_SCREEN_SHADER      = 1 << 4,
        RPR_COLOR_CONVERSION   = 1 << 5,
        RPR_ZOOM               = 1 << 6,
        RPR_OUTPUT_TRANSFORM   = 1 << 7,
        RPR_BACKEND_CONSTRAINT = 1 << 8,
    };

    struct SRenderPassExternalRequirements {
        bool outputCopy        = false;
        bool screenShader      = false;
        bool colorConversion   = false;
        bool zoom              = false;
        bool outputTransform   = false;
        bool backendConstraint = false;
    };

    class CRenderPassRequirements {
      public:
        void     add(eRenderPassRequirement requirement);
        void     add(const SRenderPassExternalRequirements& requirements);
        bool     has(eRenderPassRequirement requirement) const;
        bool     requiresOffscreen() const;
        uint32_t reasons() const;

      private:
        uint32_t m_reasons = RPR_NONE;
    };
}
