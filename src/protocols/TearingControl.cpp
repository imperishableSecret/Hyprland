#include "TearingControl.hpp"
#include "../managers/ProtocolManager.hpp"
#include "../desktop/view/Window.hpp"
#include "../event/EventBus.hpp"
#include "../Compositor.hpp"
#include "core/Compositor.hpp"

#include <algorithm>

CTearingControlProtocol::CTearingControlProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    static auto P = Event::bus()->m_events.window.destroy.listen([this](PHLWINDOWREF window) { onWindowDestroy(window.lock()); });
}

void CTearingControlProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CWpTearingControlManagerV1>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CWpTearingControlManagerV1* p) { this->onManagerResourceDestroy(p->resource()); });

    RESOURCE->setDestroy([this](CWpTearingControlManagerV1* pMgr) { this->onManagerResourceDestroy(pMgr->resource()); });
    RESOURCE->setGetTearingControl([this](CWpTearingControlManagerV1* pMgr, uint32_t id, wl_resource* surface) {
        this->onGetController(pMgr->client(), pMgr, id, CWLSurfaceResource::fromResource(surface));
    });
}

void CTearingControlProtocol::onManagerResourceDestroy(wl_resource* res) {
    std::erase_if(m_managers, [&](const auto& other) { return other->resource() == res; });
}

void CTearingControlProtocol::onGetController(wl_client* client, CWpTearingControlManagerV1* pMgr, uint32_t id, SP<CWLSurfaceResource> surf) {
    auto                ITER = std::ranges::find_if(m_tearingControllers, [&surf](const auto& controller) { return controller->m_surface == surf; });
    WP<CTearingControl> CONTROLLER;

    if (ITER != m_tearingControllers.end()) {
        if ((*ITER)->m_resource) {
            pMgr->error(WP_TEARING_CONTROL_MANAGER_V1_ERROR_TEARING_CONTROL_EXISTS, "Tearing control already exists");
            return;
        }

        (*ITER)->setResource(makeShared<CWpTearingControlV1>(client, pMgr->version(), id));
        CONTROLLER = WP<CTearingControl>{*ITER};
    } else {
        m_tearingControllers.emplace_back(makeUnique<CTearingControl>(makeShared<CWpTearingControlV1>(client, pMgr->version(), id), surf));
        CONTROLLER = WP<CTearingControl>{m_tearingControllers.back()};
    }

    if UNLIKELY (!CONTROLLER->good()) {
        pMgr->noMemory();
        onControllerDestroy(CONTROLLER.get());
        return;
    }

    CONTROLLER->m_self = CONTROLLER;
}

void CTearingControlProtocol::onControllerDestroy(CTearingControl* control) {
    std::erase_if(m_tearingControllers, [control](const auto& other) { return other.get() == control; });
}

void CTearingControlProtocol::onWindowDestroy(PHLWINDOW pWindow) {
    for (auto const& c : m_tearingControllers) {
        if (c->m_window.lock() == pWindow)
            c->m_window.reset();
    }
}

//

CTearingControl::CTearingControl(SP<CWpTearingControlV1> resource_, SP<CWLSurfaceResource> surf_) : m_surface(surf_) {
    setResource(std::move(resource_));

    m_listeners.contentUpdate  = m_surface->m_events.contentUpdate.listen([this](const WP<CContentUpdate>& update) {
        if (!m_dirty)
            return;

        const auto HINT = m_pendingHint;
        update->addActivation([self = m_self, HINT] {
            if (!self)
                return;

            self->m_hint         = HINT;
            self->m_stateChanged = true;
        });
        m_dirty = false;
    });
    m_listeners.surfaceCommit  = m_surface->m_events.commit.listen([this] {
        if (m_stateChanged) {
            updateWindow();
            m_stateChanged = false;
        }

        if (!m_resource && m_hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC)
            PROTO::tearing->onControllerDestroy(this);
    });
    m_listeners.surfaceDestroy = m_surface->m_events.destroy.listen([this] {
        m_surface.reset();
        m_window.reset();
        if (!m_resource)
            PROTO::tearing->onControllerDestroy(this);
    });

    for (auto const& w : Desktop::windowState()->windows()) {
        if (w->wlSurface()->resource() == surf_) {
            m_window = w;
            break;
        }
    }
}

void CTearingControl::setResource(SP<CWpTearingControlV1> resource) {
    m_resource = std::move(resource);
    m_resource->setData(this);
    m_resource->setOnDestroy([this](CWpTearingControlV1* res) { destroy(); });
    m_resource->setDestroy([this](CWpTearingControlV1* res) { destroy(); });
    m_resource->setSetPresentationHint([this](CWpTearingControlV1* res, wpTearingControlV1PresentationHint hint) { this->onHint(hint); });
}

void CTearingControl::onHint(wpTearingControlV1PresentationHint hint_) {
    if (!m_surface)
        return;

    m_pendingHint = hint_;
    m_dirty       = true;
}

void CTearingControl::destroy() {
    m_resource.reset();
    m_pendingHint = WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC;
    m_dirty       = true;

    if (!m_surface)
        PROTO::tearing->onControllerDestroy(this);
}

void CTearingControl::updateWindow() {
    if UNLIKELY (m_window.expired())
        return;

    m_window->m_tearingHint = m_hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
}

bool CTearingControl::good() {
    return m_resource && m_resource->resource();
}
