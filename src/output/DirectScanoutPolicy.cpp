#include "DirectScanoutPolicy.hpp"

bool Monitor::scanoutTestInputValid(const SScanoutTestPolicyState& state) {
    return state.outputAvailable && state.bufferAvailable && state.outputEnabled && state.modeAvailable && state.attachedBufferMatches && state.bufferSizeMatches;
}

bool Monitor::activeScanoutDamageIdentityMatches(const SScanoutDamagePolicyState& state) {
    return state.scanoutActive && state.surfaceMatches && state.scanoutWindowAlive && state.solitaryWindowMatches && state.solitaryRootMatches;
}

bool Monitor::canBypassCompositorDamage(const SScanoutDamagePolicyState& state) {
    return activeScanoutDamageIdentityMatches(state) && !state.hasMirrors && !state.isMirror && !state.softwareCursor && !state.captureBlocksScanout;
}
