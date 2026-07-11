#pragma once

#include "../helpers/signal/Signal.hpp"
#include "WaylandProtocol.hpp"
#include "fifo-v1.hpp"

#include <cstdint>
#include <deque>
#include <span>
#include <vector>

class CEventLoopTimer;
class CWLSurfaceResource;
struct SSurfaceState;

namespace Fifo {
    inline constexpr float WATCHDOG_FALLBACK_REFRESH_HZ = 24.F;

    struct SWatchdogOutput {
        float refreshRate = 0.F;
        float vrrMinHz    = 0.F;
        bool  enabled     = false;
        bool  tearing     = false;
        bool  vrr         = false;
    };

    enum eWatchdogClearReason : uint8_t {
        WATCHDOG_SUBMITTED = 0,
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
        SWatchdogTransition lockedQueued(uint64_t epoch, bool eligible);
        SWatchdogTransition clear(uint64_t epoch, eWatchdogClearReason reason);
        bool                armed() const;
        uint64_t            epoch() const;

      private:
        bool     m_armed = false;
        uint64_t m_epoch = 0;
    };

    float    effectiveRefresh(const SWatchdogOutput& output);
    float    slowestRelevantRefresh(std::span<const SWatchdogOutput> outputs, float fallbackRefreshRate = WATCHDOG_FALLBACK_REFRESH_HZ);
    int      watchdogTimeoutMs(float refreshRate);
    bool     submissionMatchesActiveEpoch(uint64_t activeEpoch, uint64_t submittedEpoch);
    uint64_t waitBarrierEpoch(uint64_t latestQueuedEpoch, bool activeBarrierSet, uint64_t activeBarrierEpoch);
    bool     waitConstraintApplies(uint64_t waitEpoch, bool synchronizedSubsurface);
    bool     watchdogEpochEligible(bool mapped, bool activeBarrierSet, bool activeBarrierOwned, uint64_t activeBarrierEpoch, uint64_t waitingEpoch);
}

class CFifoResource {
  public:
    CFifoResource(UP<CWpFifoV1>&& resource, SP<CWLSurfaceResource> surface);
    ~CFifoResource();

    bool good();
    void stageForOutput(PHLMONITOR monitor);
    void submitted(uint64_t epoch);
    void submissionDiscarded(uint64_t epoch);

  private:
    struct SLockedState {
        uint64_t          epoch = 0;
        WP<SSurfaceState> state;
    };

    uint64_t               nextEpoch();
    bool                   checkMonitors(bool needsSchedule = false);
    void                   armFrontWatchdog();
    void                   clearBarrier(uint64_t epoch, Fifo::eWatchdogClearReason reason);
    void                   clearAll(Fifo::eWatchdogClearReason reason);
    void                   discardState(const WP<SSurfaceState>& state);
    void                   scheduleBarrierClear();
    int                    barrierClearTimeoutMs();
    void                   destroyProtocolResource();

    UP<CWpFifoV1>          m_resource;
    WP<CFifoResource>      m_self;
    WP<CWLSurfaceResource> m_surface;

    struct {
        CHyprSignalListener surfaceStateCommit;
        CHyprSignalListener surfaceStateApplied;
        CHyprSignalListener surfaceStateDiscarded;
        CHyprSignalListener surfaceCommit;
        CHyprSignalListener surfaceUnmap;
        CHyprSignalListener surfaceDestroy;
    } m_listeners;

    SP<CEventLoopTimer>      m_barrierClearTimer;
    Fifo::CWatchdogState     m_watchdogState;
    std::deque<SLockedState> m_lockedStates;
    uint64_t                 m_nextEpoch                 = 1;
    uint64_t                 m_sampledEpoch              = 0;
    bool                     m_protocolResourceDestroyed = false;

    friend class CFifoProtocol;
    friend class CFifoManagerResource;
};

class CFifoManagerResource {
  public:
    CFifoManagerResource(UP<CWpFifoManagerV1>&& resource);
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
    void                                  destroyResource(CFifoManagerResource* resource);
    void                                  destroyResource(CFifoResource* resource);

    std::vector<UP<CFifoManagerResource>> m_managers;
    // Surface states retain FIFO semantics after the protocol object is
    // destroyed, and submission work needs a lockable weak reference.
    std::vector<SP<CFifoResource>> m_fifos;

    friend class CFifoManagerResource;
    friend class CFifoResource;
};

namespace PROTO {
    inline UP<CFifoProtocol> fifo;
};
