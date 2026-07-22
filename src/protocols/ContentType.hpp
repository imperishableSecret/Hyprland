#pragma once

#include "WaylandProtocol.hpp"
#include "core/Compositor.hpp"
#include "content-type-v1.hpp"
#include "types/ContentType.hpp"

class CContentTypeManager {
  public:
    CContentTypeManager(SP<CWpContentTypeManagerV1> resource);

    bool good();

  private:
    void                        onGetSurfaceContentType(CWpContentTypeManagerV1* resource, uint32_t id, wl_resource* surface);

    SP<CWpContentTypeManagerV1> m_resource;
};

class CContentType {
  public:
    CContentType(SP<CWpContentTypeV1> resource, SP<CWLSurfaceResource> surface);
    CContentType(WP<CWLSurfaceResource> surface);

    bool                       good();
    wl_client*                 client();
    NContentType::eContentType m_value = NContentType::CONTENT_TYPE_NONE;

    WP<CContentType>           m_self;

  private:
    SP<CWpContentTypeV1>       m_resource;
    WP<CWLSurfaceResource>     m_surface;
    wl_client*                 m_client       = nullptr;
    NContentType::eContentType m_pendingValue = NContentType::CONTENT_TYPE_NONE;
    bool                       m_dirty        = false;
    bool                       m_internal     = false;

    void                       setResource(SP<CWpContentTypeV1> resource);
    void                       destroy();

    struct {
        CHyprSignalListener contentUpdate;
        CHyprSignalListener surfaceCommit;
        CHyprSignalListener surfaceDestroy;
    } m_listeners;

    friend class CContentTypeProtocol;
    friend class CContentTypeManager;
};

class CContentTypeProtocol : public IWaylandProtocol {
  public:
    CContentTypeProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void     bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

    SP<CContentType> getContentType(WP<CWLSurfaceResource> surface);

  private:
    void                                 destroyResource(CContentTypeManager* resource);
    void                                 destroyResource(CContentType* resource);

    std::vector<SP<CContentTypeManager>> m_managers;
    std::vector<SP<CContentType>>        m_contentTypes;

    friend class CContentTypeManager;
    friend class CContentType;
};

namespace PROTO {
    inline UP<CContentTypeProtocol> contentType;
};
