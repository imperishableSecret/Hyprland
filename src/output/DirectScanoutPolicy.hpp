#pragma once

#include <cstdint>

namespace Monitor {
    enum eDirectScanoutDamageBlocker : uint8_t {
        DS_DAMAGE_BLOCK_NONE            = 0,
        DS_DAMAGE_BLOCK_MIRROR          = 1 << 0,
        DS_DAMAGE_BLOCK_SOFTWARE_CURSOR = 1 << 1,
        DS_DAMAGE_BLOCK_GLOBAL_CAPTURE  = 1 << 2,
    };

    struct SDirectScanoutDamageState {
        bool    exactActiveRoot = false;
        uint8_t blockers        = DS_DAMAGE_BLOCK_NONE;
    };

    bool canBypassCompositorDamage(const SDirectScanoutDamageState& state);
}
