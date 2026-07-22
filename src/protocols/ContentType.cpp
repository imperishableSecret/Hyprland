#include "ContentType.hpp"
#include "content-type-v1.hpp"
#include "protocols/types/ContentType.hpp"

CContentTypeManager::CContentTypeManager(SP<CWpContentTypeManagerV1> resource) : m_resource(resource) {
    if UNLIKELY (!good())
        return;

    m_resource->setDestroy([this](CWpContentTypeManagerV1* r) { PROTO::contentType->destroyResource(this); });
    m_resource->setOnDestroy([this](CWpContentTypeManagerV1* r) { PROTO::contentType->destroyResource(this); });

    m_resource->setGetSurfaceContentType([this](CWpContentTypeManagerV1* resource, uint32_t id, wl_resource* surface) { onGetSurfaceContentType(resource, id, surface); });
}

void CContentTypeManager::onGetSurfaceContentType(CWpContentTypeManagerV1* resource, uint32_t id, wl_resource* surface) {
    LOGM(Log::TRACE, "Get surface for id={}, surface={}", id, (uintptr_t)surface);
    auto SURFACE = CWLSurfaceResource::fromResource(surface);

    if (!SURFACE) {
        LOGM(Log::ERR, "No surface for resource {}", (uintptr_t)surface);
        resource->error(-1, "Invalid surface (2)");
        return;
    }

    auto CONTENT_TYPE = SURFACE->m_contentType.lock();
    if (CONTENT_TYPE && (CONTENT_TYPE->m_resource || CONTENT_TYPE->m_internal)) {
        resource->error(WP_CONTENT_TYPE_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED, "CT manager already exists");
        return;
    }

    if (CONTENT_TYPE)
        CONTENT_TYPE->setResource(makeShared<CWpContentTypeV1>(resource->client(), resource->version(), id));
    else {
        CONTENT_TYPE =
            PROTO::contentType->m_contentTypes.emplace_back(makeShared<CContentType>(makeShared<CWpContentTypeV1>(resource->client(), resource->version(), id), SURFACE));
        CONTENT_TYPE->m_self   = CONTENT_TYPE;
        SURFACE->m_contentType = CONTENT_TYPE;
    }

    if UNLIKELY (!CONTENT_TYPE->good()) {
        resource->noMemory();
        PROTO::contentType->destroyResource(CONTENT_TYPE.get());
    }
}

bool CContentTypeManager::good() {
    return m_resource->resource();
}

CContentType::CContentType(WP<CWLSurfaceResource> surface) {
    m_surface                  = surface;
    m_internal                 = true;
    m_listeners.surfaceDestroy = surface->m_events.destroy.listen([this] { PROTO::contentType->destroyResource(this); });
}

CContentType::CContentType(SP<CWpContentTypeV1> resource, SP<CWLSurfaceResource> surface) : m_surface(surface) {
    setResource(std::move(resource));

    m_listeners.contentUpdate  = m_surface->m_events.contentUpdate.listen([this](const WP<CContentUpdate>& update) {
        if (!m_dirty)
            return;

        const auto VALUE = m_pendingValue;
        update->addActivation([self = m_self, VALUE] {
            if (self)
                self->m_value = VALUE;
        });
        m_dirty = false;
    });
    m_listeners.surfaceCommit  = m_surface->m_events.commit.listen([this] {
        if (!m_resource && m_value == NContentType::CONTENT_TYPE_NONE)
            PROTO::contentType->destroyResource(this);
    });
    m_listeners.surfaceDestroy = m_surface->m_events.destroy.listen([this] {
        m_surface.reset();
        if (!m_resource)
            PROTO::contentType->destroyResource(this);
    });
}

void CContentType::setResource(SP<CWpContentTypeV1> resource) {
    m_resource = std::move(resource);
    if UNLIKELY (!good())
        return;

    m_client = m_resource->client();

    m_resource->setDestroy([this](CWpContentTypeV1* r) { destroy(); });
    m_resource->setOnDestroy([this](CWpContentTypeV1* r) { destroy(); });

    m_resource->setSetContentType([this](CWpContentTypeV1* r, wpContentTypeV1Type type) {
        m_pendingValue = NContentType::fromWP(type);
        m_dirty        = true;
    });
}

void CContentType::destroy() {
    m_resource.reset();
    m_pendingValue = NContentType::CONTENT_TYPE_NONE;
    m_dirty        = true;

    if (!m_surface)
        PROTO::contentType->destroyResource(this);
}

bool CContentType::good() {
    return m_resource && m_resource->resource();
}

wl_client* CContentType::client() {
    return m_client;
}

CContentTypeProtocol::CContentTypeProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CContentTypeProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeShared<CContentTypeManager>(makeShared<CWpContentTypeManagerV1>(client, ver, id)));

    if UNLIKELY (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
        return;
    }
}

SP<CContentType> CContentTypeProtocol::getContentType(WP<CWLSurfaceResource> surface) {
    if (surface->m_contentType.valid())
        return surface->m_contentType.lock();

    const auto RESOURCE = m_contentTypes.emplace_back(makeShared<CContentType>(surface));
    RESOURCE->m_self    = RESOURCE;
    return RESOURCE;
}

void CContentTypeProtocol::destroyResource(CContentTypeManager* resource) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == resource; });
}

void CContentTypeProtocol::destroyResource(CContentType* resource) {
    std::erase_if(m_contentTypes, [&](const auto& other) { return other.get() == resource; });
}
