#pragma once

#include "Monitor.hpp"

#include <hyprutils/os/FileDescriptor.hpp>

#include <chrono>
#include <cstdint>

namespace Monitor {
    class CMonitorFrameScheduler {
      public:
        using hrc = std::chrono::steady_clock;

        CMonitorFrameScheduler(PHLMONITOR m);

        CMonitorFrameScheduler(const CMonitorFrameScheduler&)            = delete;
        CMonitorFrameScheduler(CMonitorFrameScheduler&&)                 = delete;
        CMonitorFrameScheduler& operator=(const CMonitorFrameScheduler&) = delete;
        CMonitorFrameScheduler& operator=(CMonitorFrameScheduler&&)      = delete;

        void                    onSyncFired();
        void                    onPresented();
        void                    onFrame();
        uint64_t                renderGeneration() const;

      private:
        bool                       canRender();
        void                       onFinishRender(Hyprutils::OS::CFileDescriptor fence);
        bool                       newSchedulingEnabled();

        bool                       m_renderAtFrame          = true;
        bool                       m_pendingThird           = false;
        bool                       m_forceConventional      = false;
        uint64_t                   m_renderGeneration       = 0;
        uint64_t                   m_pendingThirdGeneration = 0;
        hrc::time_point            m_lastRenderBegun;

        PHLMONITORREF              m_monitor;

        WP<CMonitorFrameScheduler> m_self;

        friend class CMonitor;
    };
}
