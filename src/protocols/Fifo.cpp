#include "Fifo.hpp"
#include "Compositor.hpp"
#include "core/Compositor.hpp"
#include "../output/Monitor.hpp"
#include "../event/EventBus.hpp"
#include "../state/MonitorState.hpp"
#include "../managers/eventLoop/EventLoopManager.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

Fifo::SWatchdogTransition Fifo::CWatchdogState::lockedQueued(bool mapped, bool scheduled, bool tearing) {
    if (!mapped || !scheduled || tearing)
        return {};

    m_armed = true;
    return {.armTimer = true};
}

Fifo::SWatchdogTransition Fifo::CWatchdogState::clear(eWatchdogClearReason reason) {
    if (!m_armed)
        return {};

    m_armed = false;
    return {.cancelTimer = reason != WATCHDOG_EXPIRED};
}

bool Fifo::CWatchdogState::armed() const {
    return m_armed;
}

float Fifo::slowestRelevantRefresh(std::span<const SWatchdogOutput> outputs, float fallbackRefreshRate) {
    std::optional<float> slowestRefreshRate;

    for (const auto& output : outputs) {
        if (!output.enabled || output.tearing || output.refreshRate <= 0.F)
            continue;

        if (!slowestRefreshRate || output.refreshRate < *slowestRefreshRate)
            slowestRefreshRate = output.refreshRate;
    }

    return slowestRefreshRate.value_or(fallbackRefreshRate);
}

int Fifo::watchdogTimeoutMs(float refreshRate) {
    const float EFFECTIVE_REFRESH_RATE = refreshRate > 0.F ? refreshRate : 60.F;
    const int   WATCHDOG_MS            = sc<int>(std::ceil(3000.F / EFFECTIVE_REFRESH_RATE));
    return std::clamp(WATCHDOG_MS, 50, 250);
}

CFifoResource::CFifoResource(UP<CWpFifoV1>&& resource_, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource_)), m_surface(surface) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setData(this);
    m_resource->setDestroy([this](CWpFifoV1* r) { PROTO::fifo->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpFifoV1* r) { PROTO::fifo->destroyResource(this); });

    m_resource->setSetBarrier([this](CWpFifoV1* r) {
        if (!m_surface) {
            r->error(WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        m_surface->m_pending.barrierSet        = true;
        m_surface->m_pending.updated.bits.fifo = true;
    });

    m_resource->setWaitBarrier([this](CWpFifoV1* r) {
        if (!m_surface) {
            r->error(WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        if (!m_surface->m_current.barrierSet) {
            // that might mean an empty commit with a barrier_set alone
            static const auto PPEND = CConfigValue<Config::INTEGER>("debug:fifo_pending_workaround");
            if (!m_surface->m_pending.fifoScheduled)
                m_surface->m_pending.fifoScheduled = checkMonitors(*PPEND);

            return;
        }

        m_surface->m_pending.surfaceLocked = true;
    });

    m_listeners.surfaceStateCommit = m_surface->m_events.stateCommit.listen([this](auto state) {
        if (!state || !state->surfaceLocked)
            return;

        static const auto PPEND = CConfigValue<Config::INTEGER>("debug:fifo_pending_workaround");

        if (!state->fifoScheduled)
            state->fifoScheduled = checkMonitors(*PPEND);

        if (!state->fifoScheduled)
            return;

        // only lock once its mapped.
        if (!m_surface->m_mapped || state.expired())
            return;

        m_surface->m_stateQueue.lock(state, LOCK_REASON_FIFO);

        const auto TRANSITION = m_watchdogState.lockedQueued(m_surface->m_mapped, state->fifoScheduled, false);
        if (TRANSITION.armTimer)
            scheduleBarrierClear();
    });

    m_listeners.surfaceUnmap   = m_surface->m_events.unmap.listen([this] { clearBarrier(Fifo::WATCHDOG_UNMAPPED); });
    m_listeners.surfaceDestroy = m_surface->m_events.destroy.listen([this] {
        clearBarrier(Fifo::WATCHDOG_DESTROYED);
        m_surface.reset();
    });
}

CFifoResource::~CFifoResource() {
    clearBarrier(Fifo::WATCHDOG_DESTROYED);
    if (m_barrierClearTimer)
        m_barrierClearTimer->cancel();
}

bool CFifoResource::good() {
    return m_resource->resource();
}

void CFifoResource::presented() {
    clearBarrier(Fifo::WATCHDOG_PRESENTED);
}

void CFifoResource::clearBarrier(Fifo::eWatchdogClearReason reason) {
    const auto TRANSITION = m_watchdogState.clear(reason);

    if (TRANSITION.cancelTimer && m_barrierClearTimer) {
        if (g_pEventLoopManager)
            m_barrierClearTimer->updateTimeout(std::nullopt);
        else
            m_barrierClearTimer->cancel();
    }

    if (!m_surface)
        return;

    m_surface->m_current.barrierSet = false;
    m_surface->m_stateQueue.unlockFirst(LOCK_REASON_FIFO);
}

void CFifoResource::scheduleBarrierClear() {
    if (!g_pEventLoopManager) {
        clearBarrier(Fifo::WATCHDOG_EXPIRED);
        return;
    }

    if (!m_barrierClearTimer) {
        m_barrierClearTimer = makeShared<CEventLoopTimer>(
            std::nullopt,
            [this](SP<CEventLoopTimer> self, void* data) {
                if (self)
                    self->updateTimeout(std::nullopt);

                LOGM(Log::WARN, "FIFO barrier watchdog expired, unlocking surface state");
                clearBarrier(Fifo::WATCHDOG_EXPIRED);
            },
            nullptr);
        g_pEventLoopManager->addTimer(m_barrierClearTimer);
    }

    m_barrierClearTimer->updateTimeout(std::chrono::milliseconds(barrierClearTimeoutMs()));
}

int CFifoResource::barrierClearTimeoutMs() {
    std::vector<Fifo::SWatchdogOutput> outputs;

    auto                               considerMonitor = [&](PHLMONITOR mon) {
        if (!mon)
            return;

        outputs.emplace_back(Fifo::SWatchdogOutput{
            .refreshRate = mon->m_refreshRate,
            .enabled     = mon->m_enabled,
            .tearing     = mon->m_tearingState.activelyTearing,
        });
    };

    if (m_surface->m_enteredOutputs.empty() && m_surface->m_hlSurface) {
        for (auto& m : State::monitorState()->monitors()) {
            if (!m)
                continue;

            auto box = m_surface->m_hlSurface->getSurfaceBoxGlobal();
            if (box && !box->intersection({m->m_position, m->m_size}).empty())
                considerMonitor(m);
        }
    } else {
        for (auto& m : m_surface->m_enteredOutputs) {
            if (m)
                considerMonitor(m.lock());
        }
    }

    return Fifo::watchdogTimeoutMs(Fifo::slowestRelevantRefresh(outputs));
}

bool CFifoResource::checkMonitors(bool needsSchedule) {
    if (m_surface->m_enteredOutputs.empty() && m_surface->m_hlSurface) {
        for (auto& m : State::monitorState()->monitors()) {
            if (!m || !m->m_enabled)
                continue;

            auto box = m_surface->m_hlSurface->getSurfaceBoxGlobal();
            if (box && !box->intersection({m->m_position, m->m_size}).empty()) {
                if (m->m_tearingState.activelyTearing)
                    return false; // dont fifo lock on tearing.

                if (needsSchedule)
                    m->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
            }
        }
    } else {
        for (auto& m : m_surface->m_enteredOutputs) {
            if (!m)
                continue;

            if (m->m_tearingState.activelyTearing)
                return false; // dont fifo lock on tearing.

            if (needsSchedule)
                m->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
        }
    }

    return true;
}

CFifoManagerResource::CFifoManagerResource(UP<CWpFifoManagerV1>&& resource_) : m_resource(std::move(resource_)) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setDestroy([this](CWpFifoManagerV1* r) { PROTO::fifo->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpFifoManagerV1* r) { PROTO::fifo->destroyResource(this); });

    m_resource->setGetFifo([](CWpFifoManagerV1* r, uint32_t id, wl_resource* surfResource) {
        if (!surfResource) {
            r->error(-1, "No resource for fifo");
            return;
        }

        auto surf = CWLSurfaceResource::fromResource(surfResource);

        if (!surf) {
            r->error(-1, "No surface for fifo");
            return;
        }

        if (surf->m_fifo) {
            r->error(WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS, "Surface already has a fifo");
            return;
        }

        const auto& RESOURCE = PROTO::fifo->m_fifos.emplace_back(makeUnique<CFifoResource>(makeUnique<CWpFifoV1>(r->client(), r->version(), id), surf));

        if (!RESOURCE->good()) {
            r->noMemory();
            PROTO::fifo->m_fifos.pop_back();
            return;
        }

        surf->m_fifo = RESOURCE;
        LOGM(Log::DEBUG, "New fifo at {:x} for surface {:x}", (uintptr_t)RESOURCE.get(), (uintptr_t)surf.get());
    });
}

CFifoManagerResource::~CFifoManagerResource() {
    ;
}

bool CFifoManagerResource::good() {
    return m_resource->resource();
}

CFifoProtocol::CFifoProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    static auto P = Event::bus()->m_events.monitor.added.listen([this](PHLMONITOR M) {
        M->m_events.presented.listenStatic([this, m = PHLMONITORREF{M}]() {
            if (!m || !PROTO::fifo)
                return;

            onMonitorPresent(m.lock());
        });
    });
}

void CFifoProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CFifoManagerResource>(makeUnique<CWpFifoManagerV1>(client, ver, id))).get();

    if (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
        return;
    }
}

void CFifoProtocol::destroyResource(CFifoManagerResource* res) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == res; });
}

void CFifoProtocol::destroyResource(CFifoResource* res) {
    std::erase_if(m_fifos, [&](const auto& other) { return other.get() == res; });
}

void CFifoProtocol::onMonitorPresent(PHLMONITOR m) {
    if (m->m_tearingState.activelyTearing)
        return; // fifo isnt locked on tearing.

    for (const auto& fifo : m_fifos) {
        if (!fifo->m_surface)
            continue;

        if (!fifo->m_surface->m_mapped) {
            fifo->presented();
            continue;
        }

        auto it = std::ranges::find_if(fifo->m_surface->m_enteredOutputs, [m](auto& mon) { return mon == m; });
        if (it != fifo->m_surface->m_enteredOutputs.end()) {
            fifo->presented();
            continue;
        }

        if (fifo->m_surface->m_hlSurface) {
            auto box = fifo->m_surface->m_hlSurface->getSurfaceBoxGlobal();
            if (box && !box->intersection({m->m_position, m->m_size}).empty()) {
                fifo->presented();
                continue;
            }
        }
    }
}
