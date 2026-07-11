#pragma once

#include <cstdint>
#include <vector>

#include "../helpers/signal/Signal.hpp"
#include "../helpers/time/Time.hpp"
#include "WaylandProtocol.hpp"
#include "commit-timing-v1.hpp"

class CWLSurfaceResource;
class CEventLoopTimer;
struct SSurfaceState;

namespace CommitTiming {
    Time::steady_tp nextPresentation(Time::steady_tp lastPresentation, Time::steady_tp now, Time::steady_dur refreshInterval, uint32_t framesAhead = 0);
    Time::steady_tp frameWakeTime(Time::steady_tp lastPresentation, Time::steady_tp target, Time::steady_dur refreshInterval, Time::steady_tp now);
    Time::steady_tp clampTarget(Time::steady_tp now, Time::steady_dur delay);
    bool            presentationEligible(Time::steady_tp target, Time::steady_tp now, Time::steady_tp expectedPresentation, bool variableTiming);
    bool            variableTiming(bool adaptiveSync, bool activelyTearing, bool tearingEligible);
}

class CCommitTimerResource {
  public:
    CCommitTimerResource(UP<CWpCommitTimerV1>&& resource_, SP<CWLSurfaceResource> surface);

    bool good();

  private:
    UP<CWpCommitTimerV1>   m_resource;
    WP<CWLSurfaceResource> m_surface;

    friend class CCommitTimingProtocol;
    friend class CCommitTimingManagerResource;
};

class CCommitTimingManagerResource {
  public:
    CCommitTimingManagerResource(UP<CWpCommitTimingManagerV1>&& resource_);
    ~CCommitTimingManagerResource();

    bool good();

  private:
    UP<CWpCommitTimingManagerV1> m_resource;
};

class CCommitTimingProtocol : public IWaylandProtocol {
  public:
    CCommitTimingProtocol(const wl_interface* iface, const int& ver, const std::string& name);
    ~CCommitTimingProtocol();

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

    void         onSurfaceStateCommitted(SP<CWLSurfaceResource> surface, WP<SSurfaceState> state);
    void         onMonitorFrame(PHLMONITOR monitor, bool renderAhead);
    void         onMonitorTimingInvalidated(PHLMONITOR monitor);

  private:
    struct STimedState {
        uint64_t               id = 0;
        WP<CWLSurfaceResource> surface;
        WP<SSurfaceState>      state;
        PHLMONITORREF          monitor;
        Time::steady_tp        target;
        SP<CEventLoopTimer>    wakeTimer;
        bool                   wakeRequested = false;
    };

    struct STimedSurface {
        WP<CWLSurfaceResource> surface;
        CHyprSignalListener    destroy;
        CHyprSignalListener    stateDiscarded;
    };

    uint64_t                                      nextTimedStateID();
    Time::steady_dur                              refreshInterval(PHLMONITOR monitor) const;
    bool                                          usesVariableTiming(PHLMONITOR monitor) const;
    PHLMONITOR                                    refreshTimedStateMonitor(STimedState& timedState, bool* changed = nullptr);
    void                                          trackSurface(SP<CWLSurfaceResource> surface);
    void                                          scheduleState(SP<CWLSurfaceResource> surface, WP<SSurfaceState> state, Time::steady_tp target);
    void                                          onSurfaceDestroyed(const WP<CWLSurfaceResource>& surface);
    void                                          onStateDiscarded(const WP<SSurfaceState>& state);
    void                                          armTimedState(STimedState& timedState);
    void                                          wakeTimedState(uint64_t id);
    void                                          unlockTimedState(size_t index);
    void                                          discardTimedState(size_t index);
    void                                          pruneTimedStates();
    void                                          destroyResource(CCommitTimingManagerResource* resource);
    void                                          destroyResource(CCommitTimerResource* resource);

    std::vector<UP<CCommitTimingManagerResource>> m_managers;
    std::vector<UP<CCommitTimerResource>>         m_timers;
    std::vector<STimedState>                      m_timedStates;
    std::vector<STimedSurface>                    m_timedSurfaces;
    uint64_t                                      m_nextTimedStateID = 1;

    friend class CCommitTimingManagerResource;
    friend class CCommitTimerResource;
};

namespace PROTO {
    inline UP<CCommitTimingProtocol> commitTiming;
};
