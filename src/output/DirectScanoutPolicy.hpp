#pragma once

namespace Monitor {
    struct SScanoutTestPolicyState {
        bool outputAvailable       = false;
        bool bufferAvailable       = false;
        bool outputEnabled         = false;
        bool modeAvailable         = false;
        bool attachedBufferMatches = false;
        bool bufferSizeMatches     = false;
    };

    bool scanoutTestInputValid(const SScanoutTestPolicyState& state);
}
