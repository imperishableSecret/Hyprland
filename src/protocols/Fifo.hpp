#pragma once

#include <vector>
#include "WaylandProtocol.hpp"
#include "fifo-v1.hpp"

class CWLSurfaceResource;

namespace NFifo {
    bool shouldLock(const SP<CWLSurfaceResource>& surface);
}

class CFifoResource {
  public:
    CFifoResource(UP<CWpFifoV1>&& resource_, SP<CWLSurfaceResource> surface);
    ~CFifoResource();

    bool good();

  private:
    UP<CWpFifoV1>          m_resource;

    WP<CWLSurfaceResource> m_surface;

    friend class CFifoManagerResource;
    friend class CFifoProtocol;
};

class CFifoManagerResource {
  public:
    CFifoManagerResource(UP<CWpFifoManagerV1>&& resource_);
    ~CFifoManagerResource();

    bool good();

  private:
    UP<CWpFifoManagerV1> m_resource;
};

class CFifoProtocol : public IWaylandProtocol {
  public:
    CFifoProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

  private:
    void destroyResource(CFifoManagerResource* resource);
    void destroyResource(CFifoResource* resource);

    //
    std::vector<UP<CFifoManagerResource>> m_managers;
    std::vector<UP<CFifoResource>>        m_fifos;

    friend class CFifoManagerResource;
    friend class CFifoResource;
};

namespace PROTO {
    inline UP<CFifoProtocol> fifo;
};
