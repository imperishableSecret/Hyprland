#include "AlphaModifier.hpp"
#include "../desktop/view/WLSurface.hpp"
#include "../render/Renderer.hpp"
#include "alpha-modifier-v1.hpp"
#include "core/Compositor.hpp"

CAlphaModifier::CAlphaModifier(UP<CWpAlphaModifierSurfaceV1>&& resource, SP<CWLSurfaceResource> surface) : m_surface(surface) {
    setResource(std::move(resource));
}

bool CAlphaModifier::good() {
    return m_resource->resource();
}

void CAlphaModifier::setResource(UP<CWpAlphaModifierSurfaceV1>&& resource) {
    m_resource = std::move(resource);

    if UNLIKELY (!m_resource->resource())
        return;

    m_resource->setDestroy([this](CWpAlphaModifierSurfaceV1* resource) { destroy(); });
    m_resource->setOnDestroy([this](CWpAlphaModifierSurfaceV1* resource) { destroy(); });

    m_resource->setSetMultiplier([this](CWpAlphaModifierSurfaceV1* resource, uint32_t alpha) {
        if (!m_surface) {
            m_resource->error(WP_ALPHA_MODIFIER_SURFACE_V1_ERROR_NO_SURFACE, "set_multiplier called for destroyed wl_surface");
            return;
        }

        m_pendingAlpha = alpha / sc<float>(UINT32_MAX);
        m_dirty        = true;
    });

    m_pendingEnabled = true;
    m_dirty          = true;

    m_listeners.contentUpdate = m_surface->m_events.contentUpdate.listen([this](const WP<CContentUpdate>& update) {
        if (!m_dirty)
            return;

        const float ALPHA   = m_pendingAlpha;
        const bool  ENABLED = m_pendingEnabled;
        update->addActivation([self = m_self, ALPHA, ENABLED] {
            if (!self)
                return;

            self->m_currentAlpha   = ALPHA;
            self->m_currentEnabled = ENABLED;
        });
        m_dirty = false;
    });

    m_listeners.surfaceCommitted = m_surface->m_events.commit.listen([this] {
        auto        surface = Desktop::View::CWLSurface::fromResource(m_surface.lock());
        const float ALPHA   = m_currentEnabled ? m_currentAlpha : 1.F;

        if (surface && surface->m_alphaModifier != ALPHA) {
            surface->m_alphaModifier = ALPHA;
            auto box                 = surface->getSurfaceBoxGlobal();

            if (box.has_value())
                g_pHyprRenderer->damageBox(*box);
        }

        if (!m_resource && !m_currentEnabled)
            PROTO::alphaModifier->destroyAlphaModifier(this);
    });

    m_listeners.surfaceDestroyed = m_surface->m_events.destroy.listen([this] {
        if (!m_resource)
            PROTO::alphaModifier->destroyAlphaModifier(this);
    });
}

void CAlphaModifier::destroy() {
    m_resource.reset();
    m_pendingAlpha   = 1.F;
    m_pendingEnabled = false;
    m_dirty          = true;

    if (!m_surface)
        PROTO::alphaModifier->destroyAlphaModifier(this);
}

CAlphaModifierProtocol::CAlphaModifierProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CAlphaModifierProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CWpAlphaModifierV1>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CWpAlphaModifierV1* manager) { destroyManager(manager); });

    RESOURCE->setDestroy([this](CWpAlphaModifierV1* manager) { destroyManager(manager); });
    RESOURCE->setGetSurface([this](CWpAlphaModifierV1* manager, uint32_t id, wl_resource* surface) { getSurface(manager, id, CWLSurfaceResource::fromResource(surface)); });
}

void CAlphaModifierProtocol::destroyManager(CWpAlphaModifierV1* manager) {
    std::erase_if(m_managers, [&](const auto& p) { return p.get() == manager; });
}

void CAlphaModifierProtocol::destroyAlphaModifier(CAlphaModifier* modifier) {
    std::erase_if(m_alphaModifiers, [&](const auto& entry) { return entry.second.get() == modifier; });
}

void CAlphaModifierProtocol::getSurface(CWpAlphaModifierV1* manager, uint32_t id, SP<CWLSurfaceResource> surface) {
    WP<CAlphaModifier> alphaModifier;
    auto               iter = std::ranges::find_if(m_alphaModifiers, [&](const auto& entry) { return entry.second->m_surface == surface; });

    if (iter != m_alphaModifiers.end()) {
        if (iter->second->m_resource) {
            LOGM(Log::ERR, "AlphaModifier already present for surface {:x}", (uintptr_t)surface.get());
            manager->error(WP_ALPHA_MODIFIER_V1_ERROR_ALREADY_CONSTRUCTED, "AlphaModifier already present");
            return;
        } else {
            iter->second->setResource(makeUnique<CWpAlphaModifierSurfaceV1>(manager->client(), manager->version(), id));
            alphaModifier = WP<CAlphaModifier>{iter->second};
        }
    } else {
        const auto IT =
            m_alphaModifiers.emplace(surface, makeUnique<CAlphaModifier>(makeUnique<CWpAlphaModifierSurfaceV1>(manager->client(), manager->version(), id), surface)).first;
        alphaModifier = WP<CAlphaModifier>{IT->second};
    }

    alphaModifier->m_self = alphaModifier;

    if UNLIKELY (!alphaModifier->good()) {
        manager->noMemory();
        m_alphaModifiers.erase(surface);
    }
}
