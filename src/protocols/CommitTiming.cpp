#include "CommitTiming.hpp"

#include "../managers/eventLoop/EventLoopManager.hpp"
#include "../managers/eventLoop/EventLoopTimer.hpp"
#include "../output/Monitor.hpp"
#include "core/Compositor.hpp"
#include "types/SurfaceState.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

static constexpr auto COMMIT_TIMING_FALLBACK = std::chrono::milliseconds(250);
static constexpr auto FRAME_PHASE_EPSILON    = std::chrono::microseconds(100);

Time::steady_tp       CommitTiming::nextPresentation(Time::steady_tp lastPresentation, Time::steady_tp now, Time::steady_dur refreshInterval, uint32_t framesAhead) {
    const auto INTERVAL_NS = std::chrono::duration_cast<std::chrono::nanoseconds>(refreshInterval).count();
    if (lastPresentation == Time::steady_tp{} || INTERVAL_NS <= 0)
        return now;

    int64_t intervals = 1;
    if (now >= lastPresentation + refreshInterval) {
        const auto ELAPSED_NS = std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastPresentation).count();
        intervals             = ELAPSED_NS / INTERVAL_NS + 1;
    }

    return lastPresentation + refreshInterval * (intervals + framesAhead);
}

Time::steady_tp CommitTiming::frameWakeTime(Time::steady_tp lastPresentation, Time::steady_tp target, Time::steady_dur refreshInterval, Time::steady_tp now) {
    const auto INTERVAL_NS = std::chrono::duration_cast<std::chrono::nanoseconds>(refreshInterval).count();
    if (lastPresentation == Time::steady_tp{} || INTERVAL_NS <= 0)
        return std::max(now, target);

    int64_t intervals = 1;
    if (target > lastPresentation + refreshInterval) {
        const auto TARGET_NS = std::chrono::duration_cast<std::chrono::nanoseconds>(target - lastPresentation).count();
        intervals            = (TARGET_NS + INTERVAL_NS - 1) / INTERVAL_NS;
    }

    const auto PREVIOUS_PHASE = lastPresentation + refreshInterval * (intervals - 1);
    return std::max(now, PREVIOUS_PHASE + FRAME_PHASE_EPSILON);
}

Time::steady_tp CommitTiming::clampTarget(Time::steady_tp now, Time::steady_dur delay) {
    return now + std::max(delay, Time::steady_dur::zero());
}

bool CommitTiming::presentationEligible(Time::steady_tp target, Time::steady_tp now, Time::steady_tp expectedPresentation, bool variableTiming) {
    return target <= (variableTiming ? now : expectedPresentation);
}

bool CommitTiming::variableTiming(bool adaptiveSync, bool activelyTearing, bool tearingEligible) {
    return adaptiveSync || activelyTearing || tearingEligible;
}

CCommitTimerResource::CCommitTimerResource(UP<CWpCommitTimerV1>&& resource_, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource_)), m_surface(surface) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setData(this);
    m_resource->setDestroy([this](CWpCommitTimerV1* r) { PROTO::commitTiming->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpCommitTimerV1* r) { PROTO::commitTiming->destroyResource(this); });

    m_resource->setSetTimestamp([this](CWpCommitTimerV1* resource, uint32_t tvHi, uint32_t tvLo, uint32_t tvNsec) {
        if (!m_surface) {
            resource->error(WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        if (m_surface->m_pending.commitTarget.has_value()) {
            resource->error(WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS, "Timestamp is already set");
            return;
        }

        if (tvNsec > 999999999) {
            resource->error(WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP, "Timestamp nanoseconds are invalid");
            return;
        }

        const timespec TARGET = {
            .tv_sec  = sc<time_t>((sc<uint64_t>(tvHi) << 32) | sc<uint64_t>(tvLo)),
            .tv_nsec = sc<long>(tvNsec),
        };
        const auto NOW                    = Time::steadyNow();
        m_surface->m_pending.commitTarget = CommitTiming::clampTarget(NOW, Time::till(TARGET));
    });
}

uint64_t CCommitTimingProtocol::nextTimedStateID() {
    while (true) {
        const auto ID = m_nextTimedStateID++;
        if (m_nextTimedStateID == 0)
            m_nextTimedStateID = 1;

        if (ID != 0 && std::ranges::none_of(m_timedStates, [ID](const auto& timedState) { return timedState.id == ID; }))
            return ID;
    }
}

Time::steady_dur CCommitTimingProtocol::refreshInterval(PHLMONITOR monitor) const {
    if (!monitor || monitor->m_refreshRate <= 0.F)
        return {};

    return std::chrono::nanoseconds(sc<int64_t>(std::llround(1000000000.0 / monitor->m_refreshRate)));
}

bool CCommitTimingProtocol::usesVariableTiming(PHLMONITOR monitor) const {
    if (!monitor)
        return false;

    const bool ADAPTIVE_SYNC    = monitor->m_output && monitor->m_output->state->state().adaptiveSync;
    const bool TEARING_ELIGIBLE = monitor->isTearingBlocked() == 0;
    return CommitTiming::variableTiming(ADAPTIVE_SYNC, monitor->m_tearingState.activelyTearing, TEARING_ELIGIBLE);
}

PHLMONITOR CCommitTimingProtocol::refreshTimedStateMonitor(STimedState& timedState, bool* changed) {
    const auto PREVIOUS = timedState.monitor.lock();
    const auto SURFACE  = timedState.surface.lock();
    const auto CURRENT  = SURFACE ? SURFACE->timingMainOutput() : nullptr;
    const bool CHANGED  = PREVIOUS != CURRENT;

    if (changed)
        *changed = CHANGED;
    if (!CHANGED)
        return CURRENT;

    timedState.monitor.reset();
    if (CURRENT)
        timedState.monitor = CURRENT;
    timedState.wakeRequested = false;
    return CURRENT;
}

void CCommitTimingProtocol::trackSurface(SP<CWLSurfaceResource> surface) {
    if (std::ranges::any_of(m_timedSurfaces, [&surface](const auto& tracked) { return tracked.surface == surface; }))
        return;

    auto& tracked          = m_timedSurfaces.emplace_back(STimedSurface{.surface = surface});
    tracked.destroy        = surface->m_events.destroy.listen([this, surface = WP<CWLSurfaceResource>{surface}] { onSurfaceDestroyed(surface); });
    tracked.stateDiscarded = surface->m_events.stateDiscarded.listen([this](auto state) { onStateDiscarded(state); });
}

void CCommitTimingProtocol::onSurfaceStateCommitted(SP<CWLSurfaceResource> surface, WP<SSurfaceState> state) {
    if (!surface || !state || state->rejected || !state->commitTarget.has_value())
        return;

    const auto TARGET = *state->commitTarget;
    state->commitTarget.reset();
    surface->m_stateQueue.lock(state, LOCK_REASON_TIMER);
    scheduleState(surface, state, TARGET);
}

void CCommitTimingProtocol::scheduleState(SP<CWLSurfaceResource> surface, WP<SSurfaceState> state, Time::steady_tp target) {
    if (!surface || !state)
        return;

    if (!g_pEventLoopManager) {
        surface->m_stateQueue.unlock(state, LOCK_REASON_TIMER);
        return;
    }

    const auto ID    = nextTimedStateID();
    const auto TIMER = makeShared<CEventLoopTimer>(std::nullopt, [this, ID](SP<CEventLoopTimer>, void*) { wakeTimedState(ID); }, nullptr);

    auto&      timedState = m_timedStates.emplace_back(STimedState{
        .id        = ID,
        .surface   = surface,
        .state     = state,
        .target    = target,
        .wakeTimer = TIMER,
    });

    state->timer = TIMER;
    trackSurface(surface);
    g_pEventLoopManager->addTimer(TIMER);
    armTimedState(timedState);
}

void CCommitTimingProtocol::armTimedState(STimedState& timedState) {
    if (!timedState.wakeTimer)
        return;

    const auto NOW     = Time::steadyNow();
    const auto MONITOR = refreshTimedStateMonitor(timedState);
    auto       wakeAt  = std::max(NOW, timedState.target);

    if (MONITOR && !usesVariableTiming(MONITOR)) {
        const auto INTERVAL = refreshInterval(MONITOR);
        wakeAt              = CommitTiming::frameWakeTime(MONITOR->m_lastPresentationTime, timedState.target, INTERVAL, NOW);
    }

    timedState.wakeRequested = false;
    timedState.wakeTimer->updateTimeout(std::max(wakeAt - NOW, Time::steady_dur::zero()));
}

void CCommitTimingProtocol::wakeTimedState(uint64_t id) {
    pruneTimedStates();

    const auto IT = std::ranges::find(m_timedStates, id, &STimedState::id);
    if (IT == m_timedStates.end())
        return;

    const size_t INDEX   = std::distance(m_timedStates.begin(), IT);
    const auto   MONITOR = refreshTimedStateMonitor(*IT);
    if (!MONITOR) {
        if (Time::steadyNow() >= IT->target)
            unlockTimedState(INDEX);
        else
            armTimedState(*IT);
        return;
    }

    if (IT->wakeRequested) {
        unlockTimedState(INDEX);
        return;
    }

    IT->wakeRequested = true;
    MONITOR->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);

    const auto NOW            = Time::steadyNow();
    const auto TARGET_DELAY   = std::max(IT->target - NOW, Time::steady_dur::zero());
    const auto FALLBACK_DELAY = std::max(std::chrono::duration_cast<Time::steady_dur>(COMMIT_TIMING_FALLBACK), TARGET_DELAY);
    IT->wakeTimer->updateTimeout(FALLBACK_DELAY);
}

void CCommitTimingProtocol::unlockTimedState(size_t index) {
    auto timedState = std::move(m_timedStates.at(index));
    m_timedStates.erase(m_timedStates.begin() + index);

    if (timedState.wakeTimer) {
        if (g_pEventLoopManager)
            timedState.wakeTimer->updateTimeout(std::nullopt);
        else
            timedState.wakeTimer->cancel();
    }

    if (timedState.state && timedState.state->timer == timedState.wakeTimer)
        timedState.state->timer.reset();

    const auto SURFACE = timedState.surface.lock();
    if (SURFACE && timedState.state)
        SURFACE->m_stateQueue.unlock(timedState.state, LOCK_REASON_TIMER);
}

void CCommitTimingProtocol::discardTimedState(size_t index) {
    auto timedState = std::move(m_timedStates.at(index));
    m_timedStates.erase(m_timedStates.begin() + index);

    if (timedState.wakeTimer) {
        if (g_pEventLoopManager)
            timedState.wakeTimer->updateTimeout(std::nullopt);
        else
            timedState.wakeTimer->cancel();
    }

    if (timedState.state && timedState.state->timer == timedState.wakeTimer)
        timedState.state->timer.reset();
}

void CCommitTimingProtocol::pruneTimedStates() {
    for (size_t i = m_timedStates.size(); i > 0; --i) {
        if (!m_timedStates[i - 1].surface || !m_timedStates[i - 1].state)
            discardTimedState(i - 1);
    }

    std::erase_if(m_timedSurfaces, [](const auto& tracked) { return !tracked.surface; });
}

void CCommitTimingProtocol::onSurfaceDestroyed(const WP<CWLSurfaceResource>& surface) {
    if (!surface)
        return;

    for (size_t i = m_timedStates.size(); i > 0; --i) {
        if (m_timedStates[i - 1].surface == surface)
            discardTimedState(i - 1);
    }

    std::erase_if(m_timedSurfaces, [&surface](const auto& tracked) { return tracked.surface == surface; });
}

void CCommitTimingProtocol::onStateDiscarded(const WP<SSurfaceState>& state) {
    if (!state)
        return;

    for (size_t i = m_timedStates.size(); i > 0; --i) {
        if (m_timedStates[i - 1].state == state)
            discardTimedState(i - 1);
    }
}

void CCommitTimingProtocol::onMonitorTimingInvalidated(PHLMONITOR monitor) {
    if (!monitor)
        return;

    pruneTimedStates();

    for (auto& timedState : m_timedStates) {
        const auto PREVIOUS = timedState.monitor.lock();
        bool       changed  = false;
        const auto CURRENT  = refreshTimedStateMonitor(timedState, &changed);
        if (changed || PREVIOUS == monitor || CURRENT == monitor)
            armTimedState(timedState);
    }
}

void CCommitTimingProtocol::onMonitorFrame(PHLMONITOR monitor, bool renderAhead) {
    if (!monitor)
        return;

    pruneTimedStates();

    const auto NOW      = Time::steadyNow();
    const auto INTERVAL = refreshInterval(monitor);
    const bool VARIABLE = usesVariableTiming(monitor);
    // A time constraint describes the earliest presentation, not the time at
    // which surface state should wake. Release fixed-refresh state before the
    // frame whose predicted vblank satisfies the target, giving composition a
    // complete refresh interval instead of Mesa's final 500us safety margin.
    const auto EXPECTED = CommitTiming::nextPresentation(monitor->m_lastPresentationTime, NOW, INTERVAL, renderAhead ? 1 : 0);

    for (size_t i = 0; i < m_timedStates.size();) {
        bool       changed = false;
        const auto CURRENT = refreshTimedStateMonitor(m_timedStates[i], &changed);
        if (CURRENT != monitor) {
            if (changed)
                armTimedState(m_timedStates[i]);
            ++i;
            continue;
        }

        const bool ELIGIBLE = CommitTiming::presentationEligible(m_timedStates[i].target, NOW, EXPECTED, VARIABLE);
        if (ELIGIBLE) {
            unlockTimedState(i);
            continue;
        }

        armTimedState(m_timedStates[i]);
        ++i;
    }
}

bool CCommitTimerResource::good() {
    return m_resource->resource();
}

CCommitTimingManagerResource::CCommitTimingManagerResource(UP<CWpCommitTimingManagerV1>&& resource_) : m_resource(std::move(resource_)) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setData(this);
    m_resource->setDestroy([this](CWpCommitTimingManagerV1* r) { PROTO::commitTiming->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpCommitTimingManagerV1* r) { PROTO::commitTiming->destroyResource(this); });

    m_resource->setGetTimer([](CWpCommitTimingManagerV1* r, uint32_t id, wl_resource* surfResource) {
        if (!surfResource) {
            r->error(-1, "No resource for commit timing");
            return;
        }

        auto surf = CWLSurfaceResource::fromResource(surfResource);

        if (!surf) {
            r->error(-1, "No surface for commit timing");
            return;
        }

        if (surf->m_commitTimer) {
            r->error(WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS, "Surface already has a commit timing");
            return;
        }

        const auto& RESOURCE = PROTO::commitTiming->m_timers.emplace_back(makeUnique<CCommitTimerResource>(makeUnique<CWpCommitTimerV1>(r->client(), r->version(), id), surf));

        if (!RESOURCE->good()) {
            r->noMemory();
            PROTO::commitTiming->m_timers.pop_back();
            return;
        }

        surf->m_commitTimer = RESOURCE;
    });
}

CCommitTimingManagerResource::~CCommitTimingManagerResource() {
    ;
}

bool CCommitTimingManagerResource::good() {
    return m_resource->resource();
}

CCommitTimingProtocol::CCommitTimingProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

CCommitTimingProtocol::~CCommitTimingProtocol() {
    while (!m_timedStates.empty())
        discardTimedState(m_timedStates.size() - 1);
}

void CCommitTimingProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CCommitTimingManagerResource>(makeUnique<CWpCommitTimingManagerV1>(client, ver, id))).get();

    if (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
        return;
    }
}

void CCommitTimingProtocol::destroyResource(CCommitTimingManagerResource* res) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == res; });
}

void CCommitTimingProtocol::destroyResource(CCommitTimerResource* res) {
    std::erase_if(m_timers, [&](const auto& other) { return other.get() == res; });
}
