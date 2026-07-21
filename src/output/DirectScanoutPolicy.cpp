#include "DirectScanoutPolicy.hpp"

bool Monitor::canBypassCompositorDamage(const SDirectScanoutDamageState& state) {
    return state.exactActiveRoot && state.blockers == DS_DAMAGE_BLOCK_NONE;
}
