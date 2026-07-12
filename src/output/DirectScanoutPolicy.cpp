#include "DirectScanoutPolicy.hpp"

bool Monitor::scanoutTestInputValid(const SScanoutTestPolicyState& state) {
    return state.outputAvailable && state.bufferAvailable && state.outputEnabled && state.modeAvailable && state.attachedBufferMatches && state.bufferSizeMatches;
}
