#include "HyprlandSurface.hpp"
#include "../desktop/view/WLSurface.hpp"
#include "../render/Renderer.hpp"
#include "core/Compositor.hpp"
#include "hyprland-surface-v1.hpp"
#include <hyprutils/math/Region.hpp>
#include <wayland-server.h>

CHyprlandSurface::CHyprlandSurface(SP<CHyprlandSurfaceV1> resource, SP<CWLSurfaceResource> surface) : m_surface(surface) {
    setResource(std::move(resource));
}

bool CHyprlandSurface::good() const {
    return m_resource->resource();
}

void CHyprlandSurface::setResource(SP<CHyprlandSurfaceV1> resource) {
    m_resource = std::move(resource);

    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setDestroy([this](CHyprlandSurfaceV1* resource) { destroy(); });
    m_resource->setOnDestroy([this](CHyprlandSurfaceV1* resource) { destroy(); });

    m_resource->setSetOpacity([this](CHyprlandSurfaceV1* resource, uint32_t opacity) {
        if UNLIKELY (!m_surface) {
            m_resource->error(HYPRLAND_SURFACE_V1_ERROR_NO_SURFACE, "set_opacity called for destroyed wl_surface");
            return;
        }

        auto fOpacity = wl_fixed_to_double(opacity);
        if UNLIKELY (fOpacity < 0.0 || fOpacity > 1.0) {
            m_resource->error(HYPRLAND_SURFACE_V1_ERROR_OUT_OF_RANGE, "set_opacity called with an opacity value larger than 1.0 or smaller than 0.0.");
            return;
        }

        m_pendingOpacity = fOpacity;
        m_dirty          = true;
    });

    m_resource->setSetVisibleRegion([this](CHyprlandSurfaceV1* resource, wl_resource* region) {
        if (!region) {
            m_pendingVisibleRegion.clear();
            m_dirty = true;
            return;
        }

        m_pendingVisibleRegion = CWLRegionResource::fromResource(region)->m_region;
        m_dirty                = true;
    });

    m_pendingEnabled = true;
    m_dirty          = true;

    m_listeners.contentUpdate = m_surface->m_events.contentUpdate.listen([this](const WP<CContentUpdate>& update) {
        if (!m_dirty)
            return;

        const float   OPACITY = m_pendingOpacity;
        const CRegion REGION  = m_pendingVisibleRegion;
        const bool    ENABLED = m_pendingEnabled;
        update->addActivation([self = m_self, OPACITY, REGION, ENABLED] {
            if (!self)
                return;

            self->m_currentOpacity       = OPACITY;
            self->m_currentVisibleRegion = REGION;
            self->m_currentEnabled       = ENABLED;
            self->m_stateChanged         = true;
        });
        m_dirty = false;
    });

    m_listeners.surfaceCommitted = m_surface->m_events.commit.listen([this] {
        auto surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());

        if (surface && m_stateChanged) {
            surface->m_overallOpacity = m_currentEnabled ? m_currentOpacity : 1.F;
            surface->m_visibleRegion  = m_currentEnabled ? m_currentVisibleRegion : CRegion{};
            auto box                  = surface->getSurfaceBoxGlobal();

            if (box.has_value())
                g_pHyprRenderer->damageBox(*box);
            m_stateChanged = false;
        }

        if (!m_resource && !m_currentEnabled)
            PROTO::hyprlandSurface->destroySurface(this);
    });

    m_listeners.surfaceDestroyed = m_surface->m_events.destroy.listen([this] {
        if (!m_resource)
            PROTO::hyprlandSurface->destroySurface(this);
    });
}

void CHyprlandSurface::destroy() {
    m_resource.reset();
    m_pendingOpacity = 1.F;
    m_pendingVisibleRegion.clear();
    m_pendingEnabled = false;
    m_dirty          = true;

    if (!m_surface)
        PROTO::hyprlandSurface->destroySurface(this);
}

CHyprlandSurfaceProtocol::CHyprlandSurfaceProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CHyprlandSurfaceProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    auto manager = m_managers.emplace_back(makeUnique<CHyprlandSurfaceManagerV1>(client, ver, id)).get();
    manager->setOnDestroy([this](CHyprlandSurfaceManagerV1* manager) { destroyManager(manager); });

    manager->setDestroy([this](CHyprlandSurfaceManagerV1* manager) { destroyManager(manager); });
    manager->setGetHyprlandSurface(
        [this](CHyprlandSurfaceManagerV1* manager, uint32_t id, wl_resource* surface) { getSurface(manager, id, CWLSurfaceResource::fromResource(surface)); });
}

void CHyprlandSurfaceProtocol::destroyManager(CHyprlandSurfaceManagerV1* manager) {
    std::erase_if(m_managers, [&](const auto& p) { return p.get() == manager; });
}

void CHyprlandSurfaceProtocol::destroySurface(CHyprlandSurface* surface) {
    std::erase_if(m_surfaces, [&](const auto& entry) { return entry.second.get() == surface; });
}

void CHyprlandSurfaceProtocol::getSurface(CHyprlandSurfaceManagerV1* manager, uint32_t id, SP<CWLSurfaceResource> surface) {
    WP<CHyprlandSurface> hyprlandSurface;
    auto                 iter = std::ranges::find_if(m_surfaces, [&](const auto& entry) { return entry.second->m_surface == surface; });

    if (iter != m_surfaces.end()) {
        if (iter->second->m_resource) {
            LOGM(Log::ERR, "HyprlandSurface already present for surface {:x}", (uintptr_t)surface.get());
            manager->error(HYPRLAND_SURFACE_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED, "HyprlandSurface already present");
            return;
        } else {
            iter->second->setResource(makeShared<CHyprlandSurfaceV1>(manager->client(), manager->version(), id));
            hyprlandSurface = WP<CHyprlandSurface>{iter->second};
        }
    } else {
        const auto IT   = m_surfaces.emplace(surface, makeUnique<CHyprlandSurface>(makeShared<CHyprlandSurfaceV1>(manager->client(), manager->version(), id), surface)).first;
        hyprlandSurface = WP<CHyprlandSurface>{IT->second};
    }

    hyprlandSurface->m_self = hyprlandSurface;

    if UNLIKELY (!hyprlandSurface->good()) {
        manager->noMemory();
        m_surfaces.erase(surface);
    }
}
