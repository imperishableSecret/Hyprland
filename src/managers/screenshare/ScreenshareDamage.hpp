#pragma once

#include "../../output/MirrorDamageJournal.hpp"

namespace Screenshare {
    struct SFrozenCaptureDamage {
        CRegion  bufferDamage;
        CRegion  monitorDamage;
        uint64_t generation = 0;
        bool     full       = false;
    };

    bool                 captureNeedsFullDamage(bool isFirst, bool overlayCursor, bool monitorCapture);
    SFrozenCaptureDamage freezeCaptureDamage(Monitor::SMirrorDamageSnapshot snapshot, const Vector2D& bufferSize, const Vector2D& transformedSize, wl_output_transform transform,
                                             bool fullDamage);
    uint64_t             consumedCaptureGeneration(bool fullDamage, uint64_t frozenGeneration, uint64_t copyGeneration);
}
