#include "Subcompositor.hpp"
#include "Compositor.hpp"
#include <algorithm>

static void collectSubsurfaceTree(const SP<CWLSurfaceResource>& surface, std::vector<SP<CWLSurfaceResource>>& result) {
    if (!surface || std::ranges::find(result, surface) != result.end())
        return;

    result.emplace_back(surface);

    for (const auto& subsurfaceRef : surface->m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (!SUBSURFACE)
            continue;

        collectSubsurfaceTree(SUBSURFACE->m_surface.lock(), result);
    }
}

CWLSubsurfaceResource::CWLSubsurfaceResource(SP<CWlSubsurface> resource_, SP<CWLSurfaceResource> surface_, SP<CWLSurfaceResource> parent_) :
    m_surface(surface_), m_parent(parent_), m_resource(resource_) {
    if UNLIKELY (!good())
        return;

    for (const auto& subsurfaceRef : parent_->m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (SUBSURFACE)
            m_pending.zIndex = std::max(m_pending.zIndex, SUBSURFACE->m_pending.zIndex + 1);
    }

    m_resource->setOnDestroy([this](CWlSubsurface* r) { destroy(); });
    m_resource->setDestroy([this](CWlSubsurface* r) { destroy(); });

    m_resource->setSetPosition([this](CWlSubsurface* r, int32_t x, int32_t y) {
        const Vector2D POSITION{x, y};
        if (m_pending.position == POSITION)
            return;

        m_pending.position = POSITION;
        m_pending.dirty    = true;
    });

    m_resource->setSetDesync([this](CWlSubsurface* r) {
        if (!m_sync)
            return;

        std::vector<SP<CWLSurfaceResource>> surfaces;
        collectSubsurfaceTree(m_surface.lock(), surfaces);
        m_sync = false;

        bool converted = false;
        for (const auto& surface : surfaces) {
            if (surface->effectivelySynchronized())
                continue;

            surface->m_contentUpdates.convertUnreachableSynchronizedUpdates();
            converted = true;
        }

        if (converted && !surfaces.empty())
            surfaces.front()->m_contentUpdates.tryProcess();
    });
    m_resource->setSetSync([this](CWlSubsurface* r) { m_sync = true; });

    m_resource->setPlaceAbove([this](CWlSubsurface* r, wl_resource* surf) {
        if (!placeRelativeTo(CWLSurfaceResource::fromResource(surf), true))
            r->error(WL_SUBSURFACE_ERROR_BAD_SURFACE, "Invalid surface in placeAbove");
    });

    m_resource->setPlaceBelow([this](CWlSubsurface* r, wl_resource* surf) {
        if (!placeRelativeTo(CWLSurfaceResource::fromResource(surf), false))
            r->error(WL_SUBSURFACE_ERROR_BAD_SURFACE, "Invalid surface in placeBelow");
    });

    m_listeners.commitSurface = m_surface->m_events.commit.listen([this] {
        if (m_surface->m_current.texture && !m_surface->m_mapped) {
            m_surface->map();
            m_surface->m_events.map.emit();
            return;
        }

        if (!m_surface->m_current.texture && m_surface->m_mapped) {
            m_surface->m_events.unmap.emit();
            m_surface->unmap();
            return;
        }
    });
}

CWLSubsurfaceResource::~CWLSubsurfaceResource() {
    unlinkFromParent();
    m_events.destroy.emit();
    if (m_surface)
        m_surface->resetRole();
}

void CWLSubsurfaceResource::destroy() {
    unlinkFromParent();

    if (m_surface && m_surface->m_mapped) {
        m_surface->m_events.unmap.emit();
        m_surface->unmap();
    }
    m_events.destroy.emit();
    PROTO::subcompositor->destroyResource(this);
}

void CWLSubsurfaceResource::unlinkFromParent() {
    const auto PARENT = m_parent.lock();
    if (!PARENT)
        return;

    std::erase_if(PARENT->m_subsurfaces, [this](const auto& subsurface) { return !subsurface || subsurface.get() == this; });
}

bool CWLSubsurfaceResource::placeRelativeTo(const SP<CWLSurfaceResource>& reference, bool above) {
    const auto PARENT = m_parent.lock();
    if (!PARENT)
        return true;

    if (!reference || reference == m_surface)
        return false;

    std::vector<SP<CWLSubsurfaceResource>> siblings;
    siblings.reserve(PARENT->m_subsurfaces.size());
    for (const auto& subsurfaceRef : PARENT->m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (SUBSURFACE)
            siblings.emplace_back(SUBSURFACE);
    }

    const bool PARENT_REFERENCE = reference == PARENT;
    auto       referenceIt      = std::ranges::find_if(siblings, [&reference](const auto& subsurface) { return subsurface->m_surface == reference; });
    if (!PARENT_REFERENCE && referenceIt == siblings.end())
        return false;

    std::ranges::stable_sort(siblings, {}, [](const auto& subsurface) { return subsurface->m_pending.zIndex; });
    std::erase_if(siblings, [this](const auto& subsurface) { return subsurface.get() == this; });

    size_t belowCount = sc<size_t>(std::ranges::count_if(siblings, [](const auto& subsurface) { return subsurface->m_pending.zIndex < 0; }));
    size_t insertAt   = belowCount;

    if (PARENT_REFERENCE) {
        if (!above)
            ++belowCount;
    } else {
        referenceIt = std::ranges::find_if(siblings, [&reference](const auto& subsurface) { return subsurface->m_surface == reference; });
        ASSERT(referenceIt != siblings.end());

        insertAt = sc<size_t>(std::distance(siblings.begin(), referenceIt)) + (above ? 1 : 0);
        if ((*referenceIt)->m_pending.zIndex < 0)
            ++belowCount;
    }

    siblings.emplace(siblings.begin() + insertAt, m_self.lock());

    for (size_t i = 0; i < siblings.size(); ++i) {
        const int Z_INDEX = i < belowCount ? sc<int>(i) - sc<int>(belowCount) : sc<int>(i - belowCount);
        if (siblings[i]->m_pending.zIndex == Z_INDEX)
            continue;

        siblings[i]->m_pending.zIndex = Z_INDEX;
        siblings[i]->m_pending.dirty  = true;
    }

    return true;
}

Vector2D CWLSubsurfaceResource::posRelativeToParent() {
    Vector2D               pos  = m_position;
    SP<CWLSurfaceResource> surf = m_parent.lock();

    // some apps might create cycles, which I believe _technically_ are not a protocol error
    // in some cases, notably firefox likes to do that, so we keep track of what
    // surfaces we've visited and if we hit a surface we've visited we bail out.
    std::vector<SP<CWLSurfaceResource>> surfacesVisited;

    while (surf && surf->m_role->role() == SURFACE_ROLE_SUBSURFACE && std::ranges::find(surfacesVisited, surf) == surfacesVisited.end()) {
        surfacesVisited.emplace_back(surf);
        const auto SUBSURFACE = sc<CSubsurfaceRole*>(surf->m_role.get())->m_subsurface.lock();
        if (!SUBSURFACE)
            break;

        pos += SUBSURFACE->m_position;
        surf = SUBSURFACE->m_parent.lock();
    }
    return pos;
}

bool CWLSubsurfaceResource::good() {
    return m_resource->resource();
}

bool CWLSubsurfaceResource::added() const {
    return m_added;
}

bool CWLSubsurfaceResource::announced() const {
    return m_announced;
}

SP<CWLSurfaceResource> CWLSubsurfaceResource::t1Parent() {
    SP<CWLSurfaceResource>              surf = m_parent.lock();
    std::vector<SP<CWLSurfaceResource>> surfacesVisited;

    while (surf && surf->m_role->role() == SURFACE_ROLE_SUBSURFACE && std::ranges::find(surfacesVisited, surf) == surfacesVisited.end()) {
        surfacesVisited.emplace_back(surf);
        const auto SUBSURFACE = sc<CSubsurfaceRole*>(surf->m_role.get())->m_subsurface.lock();
        if (!SUBSURFACE)
            break;

        surf = SUBSURFACE->m_parent.lock();
    }
    return surf;
}

CWLSubcompositorResource::CWLSubcompositorResource(SP<CWlSubcompositor> resource_) : m_resource(resource_) {
    if UNLIKELY (!good())
        return;

    m_resource->setOnDestroy([this](CWlSubcompositor* r) { PROTO::subcompositor->destroyResource(this); });
    m_resource->setDestroy([this](CWlSubcompositor* r) { PROTO::subcompositor->destroyResource(this); });

    m_resource->setGetSubsurface([](CWlSubcompositor* r, uint32_t id, wl_resource* surface, wl_resource* parent) {
        auto SURF   = CWLSurfaceResource::fromResource(surface);
        auto PARENT = CWLSurfaceResource::fromResource(parent);

        if UNLIKELY (!SURF || !PARENT) {
            r->error(WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "Invalid surface/parent");
            return;
        }

        // the parent must be different from the child surface, otherwise bad_parent is raised (wl_subcompositor spec)
        if UNLIKELY (SURF == PARENT) {
            r->error(WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "Parent surface must be different from the child surface");
            return;
        }

        if UNLIKELY (SURF->m_role->role() != SURFACE_ROLE_UNASSIGNED) {
            r->error(WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "Surface already has a different role");
            return;
        }

        SP<CWLSurfaceResource> t1Parent = nullptr;

        if (PARENT->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
            auto subsurface = sc<CSubsurfaceRole*>(PARENT->m_role.get())->m_subsurface.lock();
            t1Parent        = subsurface->t1Parent();
        } else
            t1Parent = PARENT;

        if UNLIKELY (t1Parent == SURF) {
            r->error(WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "Bad parent, t1 parent == surf");
            return;
        }

        const auto RESOURCE =
            PROTO::subcompositor->m_surfaces.emplace_back(makeShared<CWLSubsurfaceResource>(makeShared<CWlSubsurface>(r->client(), r->version(), id), SURF, PARENT));

        if UNLIKELY (!RESOURCE->good()) {
            r->noMemory();
            PROTO::subcompositor->m_surfaces.pop_back();
            return;
        }

        RESOURCE->m_self = RESOURCE;
        SURF->m_role     = makeShared<CSubsurfaceRole>(RESOURCE);
        PARENT->m_subsurfaces.emplace_back(RESOURCE);

        LOGM(Log::DEBUG, "New wl_subsurface with id {} at {:x}", id, (uintptr_t)RESOURCE.get());
    });
}

bool CWLSubcompositorResource::good() {
    return m_resource->resource();
}

CWLSubcompositorProtocol::CWLSubcompositorProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CWLSubcompositorProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeShared<CWLSubcompositorResource>(makeShared<CWlSubcompositor>(client, ver, id)));

    if UNLIKELY (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
        return;
    }
}

void CWLSubcompositorProtocol::destroyResource(CWLSubcompositorResource* resource) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == resource; });
}

void CWLSubcompositorProtocol::destroyResource(CWLSubsurfaceResource* resource) {
    std::erase_if(m_surfaces, [&](const auto& other) { return other.get() == resource; });
}

CSubsurfaceRole::CSubsurfaceRole(SP<CWLSubsurfaceResource> sub) : m_subsurface(sub) {
    ;
}
