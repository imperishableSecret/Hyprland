#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Render::GL {
    struct SPixelPackLayout {
        size_t   destinationOffset = 0;
        size_t   rowBytes          = 0;
        size_t   scratchBytes      = 0;
        uint32_t rowLength         = 0;
        uint32_t skipPixels        = 0;
        uint32_t skipRows          = 0;
        bool     direct            = false;
    };

    std::optional<SPixelPackLayout> calculatePixelPackLayout(size_t bufferLength, size_t strideBytes, size_t rowOffset, size_t rowBytes, uint32_t bytesPerPixel, uint32_t offsetX,
                                                             uint32_t offsetY, uint32_t readWidth, uint32_t readHeight);
}
