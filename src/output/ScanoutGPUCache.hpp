#pragma once

#include <cstdint>
#include <optional>
#include <sys/types.h>

namespace Monitor {
    struct SScanoutGPUIdentity {
        uintptr_t allocator            = 0;
        uintptr_t outputBackend        = 0;
        uintptr_t compositorBackend    = 0;
        uint64_t  compositorGeneration = 0;
        int       allocatorFD          = -1;
        int       compositorFD         = -1;

        bool      operator==(const SScanoutGPUIdentity&) const = default;
        bool      valid() const;
    };

    struct SScanoutGPUTopology {
        dev_t allocatorDevice  = 0;
        dev_t compositorDevice = 0;
        bool  sameGPU          = false;
    };

    enum class eScanoutGPUCacheResult : uint8_t {
        UNKNOWN,
        SINGLE_GPU,
        MULTI_GPU,
    };

    class CScanoutGPUCache {
      public:
        eScanoutGPUCacheResult lookup(const SScanoutGPUIdentity& identity) const;
        void                   store(const SScanoutGPUIdentity& identity, const SScanoutGPUTopology& topology);
        void                   invalidate();

      private:
        std::optional<SScanoutGPUIdentity> m_identity;
        std::optional<SScanoutGPUTopology> m_topology;
    };
}
