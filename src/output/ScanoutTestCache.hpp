#pragma once

#include <cstdint>
#include <optional>

namespace Monitor {
    struct SScanoutTestState {
        uintptr_t bufferId         = 0;
        uintptr_t modeId           = 0;
        uintptr_t customModeId     = 0;
        uint32_t  bufferFormat     = 0;
        uint32_t  outputFormat     = 0;
        uint64_t  bufferModifier   = 0;
        uint64_t  ctmHash          = 0;
        uint64_t  hdrMetadataHash  = 0;
        uint64_t  gammaLutHash     = 0;
        uint64_t  degammaLutHash   = 0;
        int32_t   modeWidth        = 0;
        int32_t   modeHeight       = 0;
        uint32_t  modeRefreshRate  = 0;
        int32_t   presentationMode = 0;
        int32_t   transform        = 0;
        uint16_t  contentType      = 0;
        bool      enabled          = false;
        bool      adaptiveSync     = false;
        bool      wideColorGamut   = false;

        bool      operator==(const SScanoutTestState&) const = default;
    };

    class CScanoutTestCache {
      public:
        bool canSkip(const SScanoutTestState& state, bool outputStateChanged, bool cursorStateChanged) const;
        void accept(const SScanoutTestState& state);
        void invalidate();

      private:
        std::optional<SScanoutTestState> m_acceptedState;
    };

    bool sameBufferScanoutNeedsCommit(bool cursorCommitDue, bool vrrKeepaliveDue, bool outputStateCommitDue);
}
