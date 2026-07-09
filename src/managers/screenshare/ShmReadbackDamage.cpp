#include "ShmReadbackDamage.hpp"

#include <cstdint>
#include <vector>

using namespace Screenshare;

static constexpr uint64_t READBACK_CALL_PENALTY = 256 * 1024;
static constexpr uint64_t READBACK_ROW_PENALTY  = 256;
static constexpr size_t   MAX_SPARSE_READS      = 64;

struct SReadbackCost {
    uint64_t area  = 0;
    uint64_t rows  = 0;
    uint64_t calls = 0;

    uint64_t score() const {
        return area + rows * READBACK_ROW_PENALTY + calls * READBACK_CALL_PENALTY;
    }
};

static SReadbackCost readbackCost(const std::vector<pixman_box32_t>& rects) {
    SReadbackCost cost{.calls = rects.size()};

    for (const auto& rect : rects) {
        const auto WIDTH  = static_cast<uint64_t>(rect.x2 - rect.x1);
        const auto HEIGHT = static_cast<uint64_t>(rect.y2 - rect.y1);
        cost.area += WIDTH * HEIGHT;
        cost.rows += HEIGHT;
    }

    return cost;
}

static SReadbackCost readbackCost(const CBox& box) {
    if (box.empty())
        return {};

    return {
        .area  = static_cast<uint64_t>(box.width) * static_cast<uint64_t>(box.height),
        .rows  = static_cast<uint64_t>(box.height),
        .calls = 1,
    };
}

CRegion Screenshare::coalesceShmReadbackDamage(const CRegion& damage, const Vector2D& bufferSize) {
    CRegion clipped = damage.copy().intersect(CBox{{}, bufferSize});
    if (clipped.empty())
        return clipped;

    const auto RECTS = clipped.getRects();
    if (RECTS.size() == 1)
        return clipped;

    const CBox EXTENTS = clipped.getExtents();
    const CBox FULL{{}, bufferSize};

    const auto EXTENTS_SCORE = readbackCost(EXTENTS).score();
    const auto FULL_SCORE    = readbackCost(FULL).score();

    if (RECTS.size() >= MAX_SPARSE_READS)
        return EXTENTS_SCORE < FULL_SCORE ? CRegion{EXTENTS} : CRegion{FULL};

    const auto SPARSE_SCORE = readbackCost(RECTS).score();
    if (SPARSE_SCORE <= EXTENTS_SCORE && SPARSE_SCORE <= FULL_SCORE)
        return clipped;

    return EXTENTS_SCORE < FULL_SCORE ? CRegion{EXTENTS} : CRegion{FULL};
}
