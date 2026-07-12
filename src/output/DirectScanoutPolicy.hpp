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

    struct SScanoutDamagePolicyState {
        bool scanoutActive         = false;
        bool surfaceMatches        = false;
        bool scanoutWindowAlive    = false;
        bool solitaryWindowMatches = false;
        bool solitaryRootMatches   = false;
        bool hasMirrors            = false;
        bool isMirror              = false;
        bool softwareCursor        = false;
        bool captureBlocksScanout  = false;
    };

    bool scanoutTestInputValid(const SScanoutTestPolicyState& state);
    bool activeScanoutDamageIdentityMatches(const SScanoutDamagePolicyState& state);
    bool canBypassCompositorDamage(const SScanoutDamagePolicyState& state);
}
