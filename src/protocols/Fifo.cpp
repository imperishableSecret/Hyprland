#include "Fifo.hpp"
#include "core/Compositor.hpp"
#include "core/Subcompositor.hpp"
#include "../config/ConfigValue.hpp"
#include "../state/MonitorState.hpp"
#include "../desktop/view/View.hpp"
#include "../desktop/view/Window.hpp"
#include "../render/Renderer.hpp"

#include <algorithm>

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

bool NFifo::shouldLock(const SP<CWLSurfaceResource>& surface) {
    if (!surface || !surface->m_mapped || surface->isTearing() || isSynchronizedSubsurface(surface))
        return false;

    static const auto PINVIS = CConfigValue<Hyprlang::INT>("render:not_shown_fifo_lock");
    if (*PINVIS == 0 || !surface->m_hlSurface)
        return true;

    const auto VIEW = surface->m_hlSurface->view();
    if (!VIEW)
        return true;

    const auto WINDOW  = VIEW->type() == Desktop::View::VIEW_TYPE_WINDOW ? dynamicPointerCast<Desktop::View::CWindow>(VIEW) : nullptr;
    const bool VISIBLE = VIEW->visible() &&
        (!WINDOW || std::ranges::any_of(State::monitorState()->monitors(), [WINDOW](const auto& monitor) { return g_pHyprRenderer->shouldRenderWindow(WINDOW, monitor); }));
    if (VISIBLE)
        return true;
    if (*PINVIS == 2)
        return false;
    if (WINDOW && WINDOW->m_ruleApplicator->renderUnfocused().valueOr(false))
        return false;
    return true;
}

// what nvidia says about the empty extra barrier commit.
/*
 * If the window is not visible (occluded, monitor on standby,
 * etc), then we could be waiting for an indefinite amount of time
 * for the compositor to send a wp_presentation_feedback::presented
 * or discarded event.
 *
 * But, wp_fifo_v1 is required to unblock in finite time, so we can
 * send an extra dummy commit with a wp_fifo_v1::wait_barrier.
 *
 * If the window is visible, then the compositor will send a
 * presented event as normal, and if the window is not visible,
 * then the second commit will trigger a discarded event.
 *
 * Note that the compositor may trigger a discarded event
 * immediately, so we use wp_commit_timer_v1 above to try to
 * throttle things to a sane rate.
 *
 * Ugly as this is, Mesa relies on the same behavior, so it's
 * probably safe to treat this as the "intended" behavior.
*/

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

        m_surface->m_pending.barrierSet = true;
    });

    m_resource->setWaitBarrier([this](CWpFifoV1* r) {
        if (!m_surface) {
            r->error(WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "Surface was gone");
            return;
        }

        m_surface->m_pending.waitBarrier       = true;
        m_surface->m_pending.updated.bits.fifo = true;
    });
}

CFifoResource::~CFifoResource() {
    ;
}

bool CFifoResource::good() {
    return m_resource->resource();
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
    ;
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
