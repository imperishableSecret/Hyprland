#include "CommitTiming.hpp"
#include "core/Compositor.hpp"
#include "../output/Monitor.hpp"
#include "../managers/eventLoop/EventLoopManager.hpp"
#include "../managers/eventLoop/EventLoopTimer.hpp"
#include <algorithm>

bool NCommitTiming::validTimestamp(uint32_t tvNsec) {
    return tvNsec < 1'000'000'000;
}

std::optional<Time::steady_dur> NCommitTiming::timerDelay(const Time::steady_tp& target, const Time::steady_tp& now) {
    if (target <= now)
        return std::nullopt;

    return target - now;
}

CCommitTimerResource::CCommitTimerResource(UP<CWpCommitTimerV1>&& resource_, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource_)), m_surface(surface) {
    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setData(this);
    m_resource->setDestroy([this](CWpCommitTimerV1* r) { PROTO::commitTiming->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpCommitTimerV1* r) { PROTO::commitTiming->destroyResource(this); });

    m_resource->setSetTimestamp([this](CWpCommitTimerV1* r, uint32_t tvHi, uint32_t tvLo, uint32_t tvNsec) {
        if (!m_surface) {
            r->error(WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        if (!NCommitTiming::validTimestamp(tvNsec)) {
            r->error(WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP, "Invalid nanoseconds in timestamp");
            return;
        }

        if (m_surface->m_pending.commitTimingTarget.has_value()) {
            r->error(WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS, "Timestamp is already set");
            return;
        }

        const timespec target{
            .tv_sec  = sc<time_t>((sc<uint64_t>(tvHi) << 32) | sc<uint64_t>(tvLo)),
            .tv_nsec = sc<long>(tvNsec),
        };

        m_surface->m_pending.commitTimingTarget = Time::fromTimespec(&target);
    });

    m_listeners.surfaceContentUpdate = m_surface->m_events.contentUpdate.listen([this](const WP<CContentUpdate>& update) {
        if (!update || !m_surface)
            return;

        auto& state = update->state();
        if (!state.commitTimingTarget)
            return;

        update->addActivation([surface = m_surface] {
            if (!surface)
                return;

            for (const auto& monitor : surface->m_enteredOutputs) {
                if (monitor)
                    monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
            }
        });

        const auto DELAY = NCommitTiming::timerDelay(*state.commitTimingTarget, Time::steadyNow());
        if (!DELAY)
            return;

        update->addConstraint(eContentUpdateConstraint::TIMER);

        state.timer = makeShared<CEventLoopTimer>(
            *DELAY,
            [surface = m_surface, update](SP<CEventLoopTimer> self, void* data) {
                if (!surface || !update)
                    return;

                surface->m_contentUpdates.clearConstraint(update, eContentUpdateConstraint::TIMER);
            },
            nullptr);
        g_pEventLoopManager->addTimer(state.timer);

        if (m_surface->m_enteredOutputs.size() == 1 && m_surface->m_enteredOutputs.front())
            m_surface->m_enteredOutputs.front()->registerCommitTimingReservation(update, *state.commitTimingTarget);
    });
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
