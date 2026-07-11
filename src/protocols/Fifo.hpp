#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <unordered_map>
#include "WaylandProtocol.hpp"
#include "fifo-v1.hpp"

#include "../helpers/signal/Signal.hpp"

class CWLSurfaceResource;
class CEventLoopTimer;

namespace Fifo {
    struct SWatchdogOutput {
        float refreshRate = 0.F;
        bool  enabled     = false;
        bool  tearing     = false;
    };

    enum eWatchdogClearReason : uint8_t {
        WATCHDOG_PRESENTED = 0,
        WATCHDOG_UNMAPPED,
        WATCHDOG_DESTROYED,
        WATCHDOG_EXPIRED,
    };

    struct SWatchdogTransition {
        bool armTimer    = false;
        bool cancelTimer = false;
    };

    class CWatchdogState {
      public:
        SWatchdogTransition lockedQueued(bool mapped, bool scheduled, bool tearing);
        SWatchdogTransition clear(eWatchdogClearReason reason);
        bool                armed() const;

      private:
        bool m_armed = false;
    };

    float slowestRelevantRefresh(std::span<const SWatchdogOutput> outputs, float fallbackRefreshRate = 60.F);
    int   watchdogTimeoutMs(float refreshRate);
}

class CFifoResource {
  public:
    CFifoResource(UP<CWpFifoV1>&& resource_, SP<CWLSurfaceResource> surface);
    ~CFifoResource();

    bool good();
    void presented();

  private:
    UP<CWpFifoV1>          m_resource;

    WP<CWLSurfaceResource> m_surface;

    struct {
        CHyprSignalListener surfaceStateCommit;
        CHyprSignalListener surfaceUnmap;
        CHyprSignalListener surfaceDestroy;
    } m_listeners;

    bool                 checkMonitors(bool needsSchedule = false);
    void                 scheduleBarrierClear();
    void                 clearBarrier(Fifo::eWatchdogClearReason reason);
    int                  barrierClearTimeoutMs();

    SP<CEventLoopTimer>  m_barrierClearTimer;
    Fifo::CWatchdogState m_watchdogState;

    friend class CFifoProtocol;
    friend class CFifoManagerResource;
};

class CFifoManagerResource {
  public:
    CFifoManagerResource(UP<CWpFifoManagerV1>&& resource_);
    ~CFifoManagerResource();

    bool good();

  private:
    UP<CWpFifoManagerV1> m_resource;
};

class CFifoProtocol : public IWaylandProtocol {
  public:
    CFifoProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

  private:
    void destroyResource(CFifoManagerResource* resource);
    void destroyResource(CFifoResource* resource);

    void onMonitorPresent(PHLMONITOR m);

    //
    std::vector<UP<CFifoManagerResource>> m_managers;
    std::vector<UP<CFifoResource>>        m_fifos;

    friend class CFifoManagerResource;
    friend class CFifoResource;
};

namespace PROTO {
    inline UP<CFifoProtocol> fifo;
};
