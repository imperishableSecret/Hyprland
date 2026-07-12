#pragma once

#include "../desktop/DesktopTypes.hpp"
#include "../helpers/math/Math.hpp"

#include <cstdint>
#include <optional>
#include <wayland-server-protocol.h>

class CWLSurfaceResource;
class IHLBuffer;

namespace Monitor {
    struct SDirectScanoutSnapshot {
        uintptr_t windowIdentity  = 0;
        uintptr_t surfaceIdentity = 0;
        uintptr_t bufferIdentity  = 0;
        Vector2D  bufferSize;
        int32_t   transform                = WL_OUTPUT_TRANSFORM_NORMAL;
        uint32_t  format                   = 0;
        uint64_t  modifier                 = 0;
        uintptr_t colorManagementIdentity  = 0;
        uintptr_t imageDescriptionIdentity = 0;
        uint16_t  contentType              = 0;
        bool      dmaBuffer                = false;
        bool      surfaceIsHDR             = false;
        bool      surfaceIsScRGB           = false;

        bool      operator==(const SDirectScanoutSnapshot&) const = default;
    };

    bool directScanoutSnapshotMatches(const SDirectScanoutSnapshot& expected, const SDirectScanoutSnapshot& current);

    struct SDirectScanoutCandidate {
        PHLWINDOW              window;
        SP<CWLSurfaceResource> surface;
        SP<IHLBuffer>          buffer;
        Vector2D               bufferSize;
        wl_output_transform    transform                = WL_OUTPUT_TRANSFORM_NORMAL;
        uint32_t               format                   = 0;
        uint64_t               modifier                 = 0;
        uintptr_t              colorManagementIdentity  = 0;
        uintptr_t              imageDescriptionIdentity = 0;
        uint16_t               contentType              = 0;
        bool                   surfaceIsHDR             = false;
        bool                   surfaceIsScRGB           = false;
    };

    struct SDirectScanoutEvaluation {
        std::optional<SDirectScanoutCandidate> candidate;
        uint16_t                               blockers = 0;
    };
}
