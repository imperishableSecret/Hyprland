#pragma once

#include <vector>
#include <unordered_map>
#include "WaylandProtocol.hpp"
#include "alpha-modifier-v1.hpp"
#include "../helpers/signal/Signal.hpp"

class CWLSurfaceResource;
class CAlphaModifierProtocol;

class CAlphaModifier {
  public:
    CAlphaModifier(UP<CWpAlphaModifierSurfaceV1>&& resource_, SP<CWLSurfaceResource> surface);

    bool good();
    void setResource(UP<CWpAlphaModifierSurfaceV1>&& resource);

  private:
    UP<CWpAlphaModifierSurfaceV1> m_resource;
    WP<CWLSurfaceResource>        m_surface;
    WP<CAlphaModifier>            m_self;
    float                         m_pendingAlpha   = 1.F;
    float                         m_currentAlpha   = 1.F;
    bool                          m_pendingEnabled = false;
    bool                          m_currentEnabled = false;
    bool                          m_dirty          = false;

    void                          destroy();

    struct {
        CHyprSignalListener contentUpdate;
        CHyprSignalListener surfaceCommitted;
        CHyprSignalListener surfaceDestroyed;
    } m_listeners;

    friend class CAlphaModifierProtocol;
};

class CAlphaModifierProtocol : public IWaylandProtocol {
  public:
    CAlphaModifierProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

  private:
    void destroyManager(CWpAlphaModifierV1* res);
    void destroyAlphaModifier(CAlphaModifier* surface);
    void getSurface(CWpAlphaModifierV1* manager, uint32_t id, SP<CWLSurfaceResource> surface);

    //
    std::vector<UP<CWpAlphaModifierV1>>                            m_managers;
    std::unordered_map<WP<CWLSurfaceResource>, UP<CAlphaModifier>> m_alphaModifiers;

    friend class CAlphaModifier;
};

namespace PROTO {
    inline UP<CAlphaModifierProtocol> alphaModifier;
};
