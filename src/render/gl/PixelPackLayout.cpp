#include "PixelPackLayout.hpp"

#include <limits>

using namespace Render::GL;

std::optional<SPixelPackLayout> Render::GL::calculatePixelPackLayout(size_t bufferLength, size_t strideBytes, size_t rowOffset, size_t rowBytes, uint32_t bytesPerPixel,
                                                                     uint32_t offsetX, uint32_t offsetY, uint32_t readWidth, uint32_t readHeight) {
    if (strideBytes == 0 || rowBytes == 0 || bytesPerPixel == 0 || readWidth == 0 || readHeight == 0)
        return std::nullopt;

    constexpr auto GL_SIZE_MAX = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    if (offsetX > GL_SIZE_MAX || offsetY > GL_SIZE_MAX || readWidth > GL_SIZE_MAX || readHeight > GL_SIZE_MAX)
        return std::nullopt;

    if (rowOffset > std::numeric_limits<size_t>::max() - rowBytes || rowOffset + rowBytes > strideBytes)
        return std::nullopt;

    const auto LAST_ROW = static_cast<size_t>(offsetY) + static_cast<size_t>(readHeight) - 1;
    if (LAST_ROW > std::numeric_limits<size_t>::max() / strideBytes)
        return std::nullopt;

    const auto LAST_ROW_START = LAST_ROW * strideBytes;
    const auto ROW_END        = rowOffset + rowBytes;
    if (LAST_ROW_START > std::numeric_limits<size_t>::max() - ROW_END || LAST_ROW_START + ROW_END > bufferLength)
        return std::nullopt;

    if (rowBytes > std::numeric_limits<size_t>::max() / readHeight)
        return std::nullopt;

    SPixelPackLayout layout{
        .destinationOffset = static_cast<size_t>(offsetY) * strideBytes + rowOffset,
        .rowBytes          = rowBytes,
        .scratchBytes      = rowBytes * readHeight,
    };

    const bool HAS_LINEAR_PIXELS = static_cast<size_t>(offsetX) * bytesPerPixel == rowOffset && static_cast<size_t>(readWidth) * bytesPerPixel == rowBytes;
    const bool HAS_PACKABLE_ROW  = strideBytes % bytesPerPixel == 0 && strideBytes / bytesPerPixel <= static_cast<size_t>(std::numeric_limits<int32_t>::max());
    if (HAS_LINEAR_PIXELS && HAS_PACKABLE_ROW) {
        layout.direct     = true;
        layout.rowLength  = static_cast<uint32_t>(strideBytes / bytesPerPixel);
        layout.skipPixels = offsetX;
        layout.skipRows   = offsetY;
    }

    return layout;
}
