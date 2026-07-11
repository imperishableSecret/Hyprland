#include "Fifo.hpp"

#include "../config/ConfigValue.hpp"
#include "../managers/eventLoop/EventLoopManager.hpp"
#include "../output/FrameSubmission.hpp"
#include "../output/Monitor.hpp"
#include "../state/MonitorState.hpp"
#include "core/Compositor.hpp"
#include "core/Subcompositor.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

class CFifoSubmissionWork : public Monitor::IFrameSubmissionWork {
  public:
    CFifoSubmissionWork(SP<CFifoResource> fifo, uint64_t epoch) : m_fifo(std::move(fifo)), m_epoch(epoch) {
        ;
    }

    virtual void submitted() override {
        if (const auto FIFO = m_fifo.lock())
            FIFO->submitted(m_epoch);
    }

    virtual void presented(const Monitor::SFramePresentation&) override {
        ;
    }

    virtual void discarded() override {
        if (const auto FIFO = m_fifo.lock())
            FIFO->submissionDiscarded(m_epoch);
    }

  private:
    WP<CFifoResource> m_fifo;
    uint64_t          m_epoch = 0;
};

Fifo::SWatchdogTransition Fifo::CWatchdogState::lockedQueued(uint64_t epoch, bool eligible) {
    if (!eligible || epoch == 0 || m_armed)
        return {};

    m_armed = true;
    m_epoch = epoch;
    return {.armTimer = true};
}

Fifo::SWatchdogTransition Fifo::CWatchdogState::clear(uint64_t epoch, eWatchdogClearReason reason) {
    if (!m_armed || epoch == 0 || m_epoch != epoch)
        return {};

    m_armed = false;
    m_epoch = 0;
    return {.cancelTimer = reason != WATCHDOG_EXPIRED};
}

bool Fifo::CWatchdogState::armed() const {
    return m_armed;
}

uint64_t Fifo::CWatchdogState::epoch() const {
    return m_epoch;
}

float Fifo::effectiveRefresh(const SWatchdogOutput& output) {
    if (output.vrr)
        return output.vrrMinHz > 0.F ? output.vrrMinHz : WATCHDOG_FALLBACK_REFRESH_HZ;
    return output.refreshRate;
}

float Fifo::slowestRelevantRefresh(std::span<const SWatchdogOutput> outputs, float fallbackRefreshRate) {
    std::optional<float> slowestRefreshRate;
    for (const auto& output : outputs) {
        const float REFRESH = effectiveRefresh(output);
        if (!output.enabled || output.tearing || REFRESH <= 0.F)
            continue;
        if (!slowestRefreshRate || REFRESH < *slowestRefreshRate)
            slowestRefreshRate = REFRESH;
    }
    return slowestRefreshRate.value_or(fallbackRefreshRate);
}

int Fifo::watchdogTimeoutMs(float refreshRate) {
    const float EFFECTIVE_REFRESH_RATE = refreshRate > 0.F ? refreshRate : WATCHDOG_FALLBACK_REFRESH_HZ;
    return std::clamp(sc<int>(std::ceil(3000.F / EFFECTIVE_REFRESH_RATE)), 50, 250);
}

bool Fifo::submissionMatchesActiveEpoch(uint64_t activeEpoch, uint64_t submittedEpoch) {
    return activeEpoch != 0 && activeEpoch == submittedEpoch;
}

uint64_t Fifo::waitBarrierEpoch(uint64_t latestQueuedEpoch, bool activeBarrierSet, uint64_t activeBarrierEpoch) {
    if (latestQueuedEpoch != 0)
        return latestQueuedEpoch;
    return activeBarrierSet ? activeBarrierEpoch : 0;
}

bool Fifo::waitConstraintApplies(uint64_t waitEpoch, bool synchronizedSubsurface) {
    return waitEpoch != 0 && !synchronizedSubsurface;
}

bool Fifo::watchdogEpochEligible(bool mapped, bool activeBarrierSet, bool activeBarrierOwned, uint64_t activeBarrierEpoch, uint64_t waitingEpoch) {
    return mapped && activeBarrierSet && activeBarrierOwned && submissionMatchesActiveEpoch(activeBarrierEpoch, waitingEpoch);
}

static bool isSynchronizedSubsurface(SP<CWLSurfaceResource> surface) {
    while (surface && surface->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
        const auto SUBSURFACE = sc<CSubsurfaceRole*>(surface->m_role.get())->m_subsurface.lock();
        if (!SUBSURFACE)
            return false;
        if (SUBSURFACE->m_sync)
            return true;
        surface = SUBSURFACE->m_parent.lock();
    }
    return false;
}

CFifoResource::CFifoResource(UP<CWpFifoV1>&& resource, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource)), m_surface(surface) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setData(this);
    m_resource->setDestroy([this](CWpFifoV1*) { PROTO::fifo->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpFifoV1*) { PROTO::fifo->destroyResource(this); });

    m_resource->setSetBarrier([this](CWpFifoV1* resource) {
        if (!m_surface) {
            resource->error(WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        m_surface->m_pending.barrierSet        = true;
        m_surface->m_pending.barrierEpoch      = nextEpoch();
        m_surface->m_pending.fifoBarrierOwner  = m_self.lock();
        m_surface->m_pending.updated.bits.fifo = true;
    });

    m_resource->setWaitBarrier([this](CWpFifoV1* resource) {
        if (!m_surface) {
            resource->error(WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        const auto QUEUED_BARRIER = m_surface->m_stateQueue.latestFifoBarrier();
        const auto QUEUED_EPOCH   = QUEUED_BARRIER ? QUEUED_BARRIER->barrierEpoch : 0;
        const auto EPOCH          = Fifo::waitBarrierEpoch(QUEUED_EPOCH, m_surface->m_current.barrierSet, m_surface->m_current.barrierEpoch);
        if (EPOCH == 0) {
            static const auto PPEND = CConfigValue<Config::INTEGER>("debug:fifo_pending_workaround");
            if (!m_surface->m_pending.fifoScheduled)
                m_surface->m_pending.fifoScheduled = checkMonitors(*PPEND);
            return;
        }

        m_surface->m_pending.surfaceLocked = true;
        m_surface->m_pending.fifoWaitEpoch = EPOCH;
        m_surface->m_pending.fifoWaitOwner = QUEUED_BARRIER ? QUEUED_BARRIER->fifoBarrierOwner : m_surface->m_current.fifoBarrierOwner;
    });

    m_listeners.surfaceStateCommit = m_surface->m_events.stateCommit.listen([this](auto state) {
        if (!state || state->rejected)
            return;

        if (!state->surfaceLocked || state->fifoWaitOwner.get() != this)
            return;

        const auto SURFACE = m_surface.lock();
        if (!SURFACE || !Fifo::waitConstraintApplies(state->fifoWaitEpoch, isSynchronizedSubsurface(SURFACE)))
            return;

        static const auto PPEND = CConfigValue<Config::INTEGER>("debug:fifo_pending_workaround");
        if (!state->fifoScheduled)
            state->fifoScheduled = checkMonitors(*PPEND);
        if (!state->fifoScheduled || !m_surface->m_mapped)
            return;

        if (std::ranges::any_of(m_lockedStates, [epoch = state->fifoWaitEpoch](const auto& locked) { return locked.epoch == epoch; }))
            return;

        m_surface->m_stateQueue.lock(state, LOCK_REASON_FIFO);
        m_lockedStates.emplace_back(SLockedState{.epoch = state->fifoWaitEpoch, .state = state});
        armFrontWatchdog();
    });

    m_listeners.surfaceStateApplied   = m_surface->m_events.stateApplied.listen([this](auto state) { discardState(state); });
    m_listeners.surfaceStateDiscarded = m_surface->m_events.stateDiscarded.listen([this](auto state) { discardState(state); });
    m_listeners.surfaceCommit         = m_surface->m_events.commit.listen([this] { armFrontWatchdog(); });
    m_listeners.surfaceUnmap          = m_surface->m_events.unmap.listen([this] { clearAll(Fifo::WATCHDOG_UNMAPPED); });
    m_listeners.surfaceDestroy        = m_surface->m_events.destroy.listen([this] {
        const auto KEEP_ALIVE = m_self.lock();
        clearAll(Fifo::WATCHDOG_DESTROYED);
        m_surface.reset();
    });
}

CFifoResource::~CFifoResource() {
    if (m_barrierClearTimer)
        m_barrierClearTimer->cancel();
}

bool CFifoResource::good() {
    return m_resource && m_resource->resource();
}

uint64_t CFifoResource::nextEpoch() {
    const auto EPOCH = m_nextEpoch++;
    if (m_nextEpoch == 0)
        m_nextEpoch = 1;
    return EPOCH;
}

void CFifoResource::stageForOutput(PHLMONITOR monitor) {
    if (!monitor || !m_surface || monitor->m_tearingState.activelyTearing || !m_surface->m_current.barrierSet || m_surface->m_current.fifoBarrierOwner.get() != this)
        return;

    const auto EPOCH = m_surface->m_current.barrierEpoch;
    if (EPOCH == 0 || EPOCH == m_sampledEpoch || m_surface->timingMainOutput() != monitor)
        return;

    const auto SELF = m_self.lock();
    if (!SELF) {
        LOGM(Log::ERR, "FIFO epoch {} could not be staged because its resource owner is not lockable", EPOCH);
        return;
    }

    armFrontWatchdog();
    m_sampledEpoch = EPOCH;
    monitor->m_frameSubmissions.attach(makeShared<CFifoSubmissionWork>(SELF, EPOCH));
    LOGM(Log::TRACE, "FIFO epoch {} staged for output {} submission {}", EPOCH, monitor->m_name, monitor->m_frameSubmissions.stagedID());
}

void CFifoResource::submitted(uint64_t epoch) {
    // A successful buffer-bearing output commit is Hyprland's latching
    // boundary for the sampled surface state. Release the FIFO condition here
    // so the following state can be prepared for the next refresh instead of
    // waiting one full cycle for page-flip completion. clearBarrier performs
    // exact-epoch checks independently for the watchdog, current condition,
    // and queued lock, so a late submission must still reach it.
    clearBarrier(epoch, Fifo::WATCHDOG_SUBMITTED);
}

void CFifoResource::submissionDiscarded(uint64_t epoch) {
    if (m_sampledEpoch == epoch)
        m_sampledEpoch = 0;
}

void CFifoResource::armFrontWatchdog() {
    if (m_lockedStates.empty() || !m_surface)
        return;

    const auto EPOCH    = m_lockedStates.front().epoch;
    const auto ELIGIBLE = m_lockedStates.front().state &&
        Fifo::watchdogEpochEligible(m_surface->m_mapped, m_surface->m_current.barrierSet, m_surface->m_current.fifoBarrierOwner.get() == this, m_surface->m_current.barrierEpoch,
                                    EPOCH);
    const auto TRANSITION = m_watchdogState.lockedQueued(EPOCH, ELIGIBLE);
    if (TRANSITION.armTimer)
        scheduleBarrierClear();
}

void CFifoResource::clearBarrier(uint64_t epoch, Fifo::eWatchdogClearReason reason) {
    if (epoch == 0)
        return;

    const auto KEEP_ALIVE = m_self.lock();
    const auto TRANSITION = m_watchdogState.clear(epoch, reason);
    if (TRANSITION.cancelTimer && m_barrierClearTimer) {
        if (g_pEventLoopManager)
            m_barrierClearTimer->updateTimeout(std::nullopt);
        else
            m_barrierClearTimer->cancel();
    }

    if (!m_surface)
        return;

    if (m_surface->m_current.barrierSet && m_surface->m_current.fifoBarrierOwner.get() == this && Fifo::submissionMatchesActiveEpoch(m_surface->m_current.barrierEpoch, epoch)) {
        m_surface->m_current.barrierSet   = false;
        m_surface->m_current.barrierEpoch = 0;
        m_surface->m_current.fifoBarrierOwner.reset();
    }

    const auto LOCK = std::ranges::find(m_lockedStates, epoch, &SLockedState::epoch);
    if (LOCK != m_lockedStates.end()) {
        const auto STATE = LOCK->state;
        m_lockedStates.erase(LOCK);
        m_surface->m_stateQueue.unlock(STATE, LOCK_REASON_FIFO);
    }

    if (m_sampledEpoch == epoch)
        m_sampledEpoch = 0;
    armFrontWatchdog();
}

void CFifoResource::clearAll(Fifo::eWatchdogClearReason reason) {
    const auto KEEP_ALIVE = m_self.lock();
    while (!m_lockedStates.empty())
        clearBarrier(m_lockedStates.front().epoch, reason);

    if (m_surface) {
        if (m_surface->m_current.fifoBarrierOwner.get() == this) {
            m_surface->m_current.barrierSet   = false;
            m_surface->m_current.barrierEpoch = 0;
            m_surface->m_current.fifoBarrierOwner.reset();
        }
        m_surface->m_stateQueue.tryProcess();
    }
    m_sampledEpoch = 0;
}

void CFifoResource::discardState(const WP<SSurfaceState>& state) {
    if (!state)
        return;

    const auto KEEP_ALIVE = m_self.lock();
    if (state->fifoWaitOwner.get() != this)
        return;

    const auto LOCK = std::ranges::find(m_lockedStates, state, &SLockedState::state);
    if (LOCK == m_lockedStates.end())
        return;

    const auto EPOCH      = LOCK->epoch;
    const auto TRANSITION = m_watchdogState.clear(EPOCH, Fifo::WATCHDOG_DESTROYED);
    if (TRANSITION.cancelTimer && m_barrierClearTimer) {
        if (g_pEventLoopManager)
            m_barrierClearTimer->updateTimeout(std::nullopt);
        else
            m_barrierClearTimer->cancel();
    }

    m_lockedStates.erase(LOCK);
    armFrontWatchdog();
}

void CFifoResource::destroyProtocolResource() {
    if (m_protocolResourceDestroyed)
        return;

    m_protocolResourceDestroyed = true;
    if (const auto SURFACE = m_surface.lock(); SURFACE && SURFACE->m_fifo.get() == this)
        SURFACE->m_fifo.reset();

    if (m_resource && m_resource->resource())
        wl_resource_destroy(m_resource->resource());
}

void CFifoResource::scheduleBarrierClear() {
    if (!g_pEventLoopManager) {
        clearBarrier(m_watchdogState.epoch(), Fifo::WATCHDOG_EXPIRED);
        return;
    }

    if (!m_barrierClearTimer) {
        m_barrierClearTimer = makeShared<CEventLoopTimer>(
            std::nullopt,
            [this](SP<CEventLoopTimer> self, void*) {
                if (self)
                    self->updateTimeout(std::nullopt);
                const auto EPOCH = m_watchdogState.epoch();
                LOGM(Log::WARN, "FIFO barrier watchdog expired for epoch {}, unlocking its surface state", EPOCH);
                clearBarrier(EPOCH, Fifo::WATCHDOG_EXPIRED);
            },
            nullptr);
        g_pEventLoopManager->addTimer(m_barrierClearTimer);
    }

    m_barrierClearTimer->updateTimeout(std::chrono::milliseconds(barrierClearTimeoutMs()));
}

int CFifoResource::barrierClearTimeoutMs() {
    std::vector<Fifo::SWatchdogOutput> outputs;
    auto                               considerMonitor = [&](PHLMONITOR monitor) {
        if (!monitor)
            return;
        outputs.emplace_back(Fifo::SWatchdogOutput{
            .refreshRate = monitor->m_refreshRate,
            .vrrMinHz    = sc<float>(monitor->m_vrrMinHz),
            .enabled     = monitor->m_enabled,
            .tearing     = monitor->m_tearingState.activelyTearing,
            .vrr         = monitor->m_output && monitor->m_output->state->state().adaptiveSync,
        });
    };

    if (m_surface->m_enteredOutputs.empty() && m_surface->m_hlSurface) {
        for (const auto& monitor : State::monitorState()->monitors()) {
            const auto BOX = m_surface->m_hlSurface->getSurfaceBoxGlobal();
            if (monitor && BOX && !BOX->intersection({monitor->m_position, monitor->m_size}).empty())
                considerMonitor(monitor);
        }
    } else {
        for (const auto& monitor : m_surface->m_enteredOutputs)
            considerMonitor(monitor.lock());
    }

    return Fifo::watchdogTimeoutMs(Fifo::slowestRelevantRefresh(outputs));
}

bool CFifoResource::checkMonitors(bool needsSchedule) {
    bool relevant = false;
    auto consider = [&](PHLMONITOR monitor) {
        if (!monitor || !monitor->m_enabled)
            return true;
        relevant = true;
        if (monitor->m_tearingState.activelyTearing)
            return false;
        if (needsSchedule)
            monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
        return true;
    };

    if (m_surface->m_enteredOutputs.empty() && m_surface->m_hlSurface) {
        for (const auto& monitor : State::monitorState()->monitors()) {
            const auto BOX = m_surface->m_hlSurface->getSurfaceBoxGlobal();
            if (monitor && BOX && !BOX->intersection({monitor->m_position, monitor->m_size}).empty() && !consider(monitor))
                return false;
        }
    } else {
        for (const auto& monitor : m_surface->m_enteredOutputs) {
            if (!consider(monitor.lock()))
                return false;
        }
    }
    return relevant;
}

CFifoManagerResource::CFifoManagerResource(UP<CWpFifoManagerV1>&& resource) : m_resource(std::move(resource)) {
    if UNLIKELY (!m_resource->resource())
        return;
    m_resource->setDestroy([this](CWpFifoManagerV1*) { PROTO::fifo->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpFifoManagerV1*) { PROTO::fifo->destroyResource(this); });
    m_resource->setGetFifo([](CWpFifoManagerV1* manager, uint32_t id, wl_resource* surfaceResource) {
        if (!surfaceResource) {
            manager->error(-1, "No resource for fifo");
            return;
        }

        const auto SURFACE = CWLSurfaceResource::fromResource(surfaceResource);
        if (!SURFACE) {
            manager->error(-1, "No surface for fifo");
            return;
        }
        if (SURFACE->m_fifo) {
            manager->error(WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS, "Surface already has a fifo");
            return;
        }

        const auto& RESOURCE = PROTO::fifo->m_fifos.emplace_back(makeShared<CFifoResource>(makeUnique<CWpFifoV1>(manager->client(), manager->version(), id), SURFACE));
        RESOURCE->m_self     = RESOURCE;
        if (!RESOURCE->good()) {
            manager->noMemory();
            PROTO::fifo->m_fifos.pop_back();
            return;
        }
        SURFACE->m_fifo = RESOURCE;
    });
}

CFifoManagerResource::~CFifoManagerResource() = default;

bool CFifoManagerResource::good() {
    return m_resource->resource();
}

CFifoProtocol::CFifoProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CFifoProtocol::bindManager(wl_client* client, void*, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CFifoManagerResource>(makeUnique<CWpFifoManagerV1>(client, ver, id))).get();
    if (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
    }
}

void CFifoProtocol::destroyResource(CFifoManagerResource* resource) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == resource; });
}

void CFifoProtocol::destroyResource(CFifoResource* resource) {
    const auto OWNER = std::ranges::find(m_fifos, resource, [](const auto& other) { return other.get(); });
    if (OWNER == m_fifos.end())
        return;

    const auto KEEP_ALIVE = *OWNER;
    KEEP_ALIVE->destroyProtocolResource();
    std::erase_if(m_fifos, [&](const auto& other) { return other.get() == resource; });
}
