#include "Compositor.hpp"
#include "../../Compositor.hpp"
#include "Output.hpp"
#include "Seat.hpp"
#include "../types/WLBuffer.hpp"
#include <algorithm>
#include <array>
#include <ranges>
#include "Subcompositor.hpp"
#include "../Viewporter.hpp"
#include "../../output/Monitor.hpp"
#include "../PresentationTime.hpp"
#include "../Fifo.hpp"
#include "../DRMSyncobj.hpp"
#include "../types/DMABuffer.hpp"
#include "../../render/Renderer.hpp"
#include "config/ConfigValue.hpp"
#include "../../managers/eventLoop/EventLoopManager.hpp"
#include "../../state/MonitorState.hpp"
#include "protocols/types/SurfaceRole.hpp"
#include "render/Texture.hpp"
#include <cstring>

using namespace NColorManagement;

static bool addSafeDamage(CRegion& damage, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0)
        return false;

    const int64_t x2 = std::min<int64_t>(sc<int64_t>(x) + w, INT32_MAX);
    const int64_t y2 = std::min<int64_t>(sc<int64_t>(y) + h, INT32_MAX);

    if (x2 <= x || y2 <= y)
        return false;

    damage.add(x, y, x2 - x, y2 - y);
    return true;
}

static size_t subsurfaceDepth(const SP<CWLSurfaceResource>& surface) {
    auto                                current = surface;
    std::vector<SP<CWLSurfaceResource>> visited;
    size_t                              depth = 0;

    while (current && current->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
        if (std::ranges::find(visited, current) != visited.end())
            break;

        visited.emplace_back(current);

        const auto SUBSURFACE = sc<CSubsurfaceRole*>(current->m_role.get())->m_subsurface.lock();
        if (!SUBSURFACE)
            break;

        current = SUBSURFACE->m_parent.lock();
        ++depth;
    }

    return depth;
}

class CDefaultSurfaceRole : public ISurfaceRole {
  public:
    virtual eSurfaceRole role() {
        return SURFACE_ROLE_UNASSIGNED;
    }
};

CWLCallbackResource::CWLCallbackResource(SP<CWlCallback>&& resource_) : m_resource(std::move(resource_)) {
    ;
}

bool CWLCallbackResource::good() {
    return m_resource && m_resource->resource();
}

void CWLCallbackResource::send(const Time::steady_tp& now) {
    if (!good())
        return;

    m_resource->sendDone(Time::millis(now));
    m_resource.reset();
}

CWLRegionResource::CWLRegionResource(SP<CWlRegion> resource_) : m_resource(resource_) {
    if UNLIKELY (!good())
        return;

    m_resource->setData(this);

    m_resource->setDestroy([this](CWlRegion* r) { PROTO::compositor->destroyResource(this); });
    m_resource->setOnDestroy([this](CWlRegion* r) { PROTO::compositor->destroyResource(this); });

    m_resource->setAdd([this](CWlRegion* r, int32_t x, int32_t y, int32_t w, int32_t h) { m_region.add(CBox{x, y, w, h}); });
    m_resource->setSubtract([this](CWlRegion* r, int32_t x, int32_t y, int32_t w, int32_t h) { m_region.subtract(CBox{x, y, w, h}); });
}

bool CWLRegionResource::good() {
    return m_resource->resource();
}

SP<CWLRegionResource> CWLRegionResource::fromResource(wl_resource* res) {
    auto data = sc<CWLRegionResource*>(sc<CWlRegion*>(wl_resource_get_user_data(res))->data());
    return data ? data->m_self.lock() : nullptr;
}

CWLSurfaceResource::CWLSurfaceResource(SP<CWlSurface> resource_) : m_resource(resource_) {
    if UNLIKELY (!good())
        return;

    m_client = m_resource->client();

    m_resource->setData(this);

    m_role = makeShared<CDefaultSurfaceRole>();

    m_resource->setDestroy([this](CWlSurface* r) { destroy(); });
    m_resource->setOnDestroy([this](CWlSurface* r) { destroy(); });

    m_resource->setAttach([this](CWlSurface* r, wl_resource* buffer, int32_t x, int32_t y) {
        // version is 5 or higher, passing any non-zero x or y is a protocol violation
        if (m_resource->version() >= 5 && (x != 0 || y != 0)) {
            r->error(WL_SURFACE_ERROR_INVALID_OFFSET, "attach x and y must be 0 since version 5");
            return;
        }

        m_pending.updated.bits.buffer = true;
        m_pending.updated.bits.offset = true;

        m_pending.offset = {x, y};

        if (m_pending.buffer)
            m_pending.buffer.drop();

        auto buf = buffer ? CWLBufferResource::fromResource(buffer) : nullptr;

        if (buf && buf->m_buffer) {
            m_pending.buffer     = CHLBufferReference(buf->m_buffer.lock());
            m_pending.texture    = buf->m_buffer->m_texture;
            m_pending.size       = buf->m_buffer->size;
            m_pending.bufferSize = buf->m_buffer->size;
        } else {
            m_pending.buffer = {};
            m_pending.texture.reset();
            m_pending.size       = Vector2D{};
            m_pending.bufferSize = Vector2D{};
        }

        if (m_pending.bufferSize != m_current.bufferSize) {
            m_pending.updated.bits.damage = true;
            m_pending.bufferDamage        = CBox{{}, m_pending.bufferSize};
        }
    });

    m_resource->setCommit([this](CWlSurface* r) {
        if (m_pending.buffer)
            m_pending.bufferDamage.intersect(CBox{{}, m_pending.bufferSize});

        if (!m_pending.buffer)
            m_pending.size = {};
        else if (m_pending.viewport.hasDestination)
            m_pending.size = m_pending.viewport.destination;
        else if (m_pending.viewport.hasSource)
            m_pending.size = m_pending.viewport.source.size();
        else {
            // without a viewport, at commit time the supplied
            // buffer size must be an integer multiple of the buffer_scale. If
            // that's not the case, an invalid_size error is sent.
            if (sc<int>(m_pending.bufferSize.x) % m_pending.scale != 0 || sc<int>(m_pending.bufferSize.y) % m_pending.scale != 0) {
                r->error(WL_SURFACE_ERROR_INVALID_SIZE, "buffer size is not an integer multiple of the buffer scale");
                dropPendingBuffer();
                return;
            }

            Vector2D tfs   = m_pending.transform % 2 == 1 ? Vector2D{m_pending.bufferSize.y, m_pending.bufferSize.x} : m_pending.bufferSize;
            m_pending.size = tfs / m_pending.scale;
        }

        if (m_pending.size.x <= 0 || m_pending.size.y <= 0)
            m_pending.damage.clear();
        else
            m_pending.damage.intersect(CBox{{}, m_pending.size});

        m_events.precommit.emit();

        const auto MODE   = effectivelySynchronized() ? eContentUpdateMode::SYNCHRONIZED : eContentUpdateMode::DESYNCHRONIZED;
        auto       update = m_contentUpdates.enqueue(makeUnique<CContentUpdate>(m_pending, m_self, MODE));
        m_pending.reset();
        attachSynchronizedChildren(update);
        prepareSubsurfaceState(*update);
        prepareFifoState(*update);

        m_events.contentUpdate.emit(update);

        auto& state = update->state();
        if (state.buffer && state.buffer->type() == Aquamarine::BUFFER_TYPE_DMABUF && state.buffer->dmabuf().success && !state.updated.bits.acquire) {
            state.buffer->m_syncFds = dc<CDMABuffer*>(state.buffer.m_buffer.get())->exportSyncFiles();
            if (!state.buffer->m_syncFds.empty())
                m_contentUpdates.addConstraint(update, eContentUpdateConstraint::FENCE);
        }

        if (state.rejected) {
            m_contentUpdates.drop(update);
            return;
        }

        scheduleUpdate(update);
        m_contentUpdates.finalize(update);
    });

    m_resource->setDamage([this](CWlSurface* r, int32_t x, int32_t y, int32_t w, int32_t h) {
        if (addSafeDamage(m_pending.damage, x, y, w, h))
            m_pending.updated.bits.damage = true;
    });
    m_resource->setDamageBuffer([this](CWlSurface* r, int32_t x, int32_t y, int32_t w, int32_t h) {
        if (addSafeDamage(m_pending.bufferDamage, x, y, w, h))
            m_pending.updated.bits.damage = true;
    });

    m_resource->setSetBufferScale([this](CWlSurface* r, int32_t scale) {
        if (scale <= 0) {
            r->error(WL_SURFACE_ERROR_INVALID_SCALE, "buffer scale must be positive");
            return;
        }

        if (scale == m_pending.scale)
            return;

        m_pending.updated.bits.scale  = true;
        m_pending.updated.bits.damage = true;

        m_pending.scale        = scale;
        m_pending.bufferDamage = CBox{{}, m_pending.bufferSize};
    });

    m_resource->setSetBufferTransform([this](CWlSurface* r, uint32_t tr) {
        if (tr > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
            r->error(WL_SURFACE_ERROR_INVALID_TRANSFORM, "invalid buffer transform");
            return;
        }

        if (tr == m_pending.transform)
            return;

        m_pending.updated.bits.transform = true;
        m_pending.updated.bits.damage    = true;

        m_pending.transform    = sc<wl_output_transform>(tr);
        m_pending.bufferDamage = CBox{{}, m_pending.bufferSize};
    });

    m_resource->setSetInputRegion([this](CWlSurface* r, wl_resource* region) {
        m_pending.updated.bits.input = true;

        if (!region) {
            m_pending.inputIsInfinite = true;
            m_pending.input.clear();
            return;
        }

        auto RG                   = CWLRegionResource::fromResource(region);
        m_pending.inputIsInfinite = false;
        m_pending.input           = RG->m_region;
    });

    m_resource->setSetOpaqueRegion([this](CWlSurface* r, wl_resource* region) {
        m_pending.updated.bits.opaque = true;

        if (!region) {
            m_pending.opaque = CBox{{}, {}};
            return;
        }

        auto RG          = CWLRegionResource::fromResource(region);
        m_pending.opaque = RG->m_region;
    });

    m_resource->setFrame([this](CWlSurface* r, uint32_t id) {
        m_pending.updated.bits.frame = true;
        m_pending.callbacks.emplace_back(makeShared<CWLCallbackResource>(makeShared<CWlCallback>(m_client, 1, id)));
    });

    m_resource->setOffset([this](CWlSurface* r, int32_t x, int32_t y) {
        m_pending.updated.bits.offset = true;
        m_pending.offset              = {x, y};
    });
}

CWLSurfaceResource::~CWLSurfaceResource() {
    if (m_fifoEmergencyTimer)
        m_fifoEmergencyTimer->cancel();
    discardPresentationFeedbacks();
    m_events.destroy.emit();
}

void CWLSurfaceResource::discardPresentationFeedbacks() {
    PROTO::presentation->discardFeedbacks(m_pending.presentationFeedbacks);
    PROTO::presentation->discardFeedbacks(m_current.presentationFeedbacks);
    PROTO::presentation->discardFeedbacksForSurface(m_self);
}

void CWLSurfaceResource::destroy() {
    m_contentUpdates.clear();
    discardPresentationFeedbacks();

    if (m_mapped) {
        m_events.unmap.emit();
        unmap();
    }
    m_events.destroy.emit();
    releaseBuffers(false);
    PROTO::compositor->destroyResource(this);
}

void CWLSurfaceResource::dropPendingBuffer() {
    m_pending.buffer = {};
}

void CWLSurfaceResource::dropCurrentBuffer() {
    m_current.buffer = {};
}

SP<CWLSurfaceResource> CWLSurfaceResource::fromResource(wl_resource* res) {
    auto data = sc<CWLSurfaceResource*>(sc<CWlSurface*>(wl_resource_get_user_data(res))->data());
    return data ? data->m_self.lock() : nullptr;
}

bool CWLSurfaceResource::good() {
    return m_resource->resource();
}

wl_client* CWLSurfaceResource::client() {
    return m_client;
}

void CWLSurfaceResource::enter(PHLMONITOR monitor) {
    if (std::ranges::find(m_enteredOutputs, monitor) != m_enteredOutputs.end())
        return;

    if UNLIKELY (!PROTO::outputs.contains(monitor->m_name)) {
        // can happen on unplug/replug
        LOGM(Log::ERR, "enter() called on a non-existent output global");
        return;
    }

    if UNLIKELY (PROTO::outputs.at(monitor->m_name)->isDefunct()) {
        LOGM(Log::ERR, "enter() called on a defunct output global");
        return;
    }

    auto outputs = PROTO::outputs.at(monitor->m_name)->outputResourcesFrom(m_client);

    if UNLIKELY (outputs.empty() || std::ranges::all_of(outputs, [](const auto& o) { return !o->getResource() || !o->getResource()->resource(); })) {
        LOGM(Log::ERR, "Cannot enter surface {:x} to {}, client hasn't bound the output", (uintptr_t)this, monitor->m_name);
        return;
    }

    m_enteredOutputs.emplace_back(monitor);

    for (const auto& o : outputs) {
        if (!o->getResource() || !o->getResource()->resource())
            continue;
        m_resource->sendEnter(o->getResource().get());
    }
    m_events.enter.emit(monitor);
}

void CWLSurfaceResource::leave(PHLMONITOR monitor) {
    if UNLIKELY (std::ranges::find(m_enteredOutputs, monitor) == m_enteredOutputs.end())
        return;

    auto outputs = PROTO::outputs.at(monitor->m_name)->outputResourcesFrom(m_client);

    if UNLIKELY (outputs.empty() || std::ranges::all_of(outputs, [](const auto& o) { return !o->getResource() || !o->getResource()->resource(); })) {
        LOGM(Log::ERR, "Cannot leave surface {:x} from {}, client hasn't bound the output", (uintptr_t)this, monitor->m_name);
        return;
    }

    std::erase(m_enteredOutputs, monitor);

    for (const auto& o : outputs) {
        if (!o->getResource() || !o->getResource()->resource())
            continue;
        m_resource->sendLeave(o->getResource().get());
    }
    m_events.leave.emit(monitor);
}

void CWLSurfaceResource::sendPreferredTransform(wl_output_transform t) {
    if (m_resource->version() < 6 || (m_lastTransform && *m_lastTransform == t))
        return;

    m_lastTransform = t;
    m_resource->sendPreferredBufferTransform(t);
}

void CWLSurfaceResource::sendPreferredScale(int32_t scale) {
    if (m_resource->version() < 6 || scale == m_lastScale.value_or(-1))
        return;

    m_lastScale = scale;
    m_resource->sendPreferredBufferScale(scale);
}

void CWLSurfaceResource::frame(const Time::steady_tp& now) {
    if (m_current.callbacks.empty())
        return;

    for (auto const& c : m_current.callbacks) {
        c->send(now);
    }

    m_current.callbacks.clear();
}

void CWLSurfaceResource::resetRole() {
    m_role = makeShared<CDefaultSurfaceRole>();
}

void CWLSurfaceResource::bfHelper(std::span<const SP<CWLSurfaceResource>> nodes, std::function<void(SP<CWLSurfaceResource>, const Vector2D&, void*)> fn, void* data) {
    std::vector<SP<CWLSurfaceResource>> nodes2;

    // first, gather all nodes below
    for (auto const& n : nodes) {
        std::erase_if(n->m_subsurfaces, [](const auto& e) { return e.expired(); });

        // subsurfaces is sorted lowest -> highest
        for (auto const& subsurfaceRef : n->m_subsurfaces) {
            const auto subsurface = subsurfaceRef.lock();
            if (!subsurface || !subsurface->added())
                continue;

            if (subsurface->m_zIndex >= 0)
                break;

            const auto surface = subsurface->m_surface.lock();
            if (!surface)
                continue;

            if (nodes2.empty())
                nodes2.reserve(nodes.size() * 2);

            nodes2.emplace_back(surface);
        }
    }

    if (!nodes2.empty())
        bfHelper(nodes2, fn, data);

    nodes2.clear();

    for (auto const& n : nodes) {
        Vector2D offset = {};
        if (n->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
            auto subsurface = sc<CSubsurfaceRole*>(n->m_role.get())->m_subsurface.lock();
            if (subsurface)
                offset = subsurface->posRelativeToParent();
        }

        fn(n, offset, data);
    }

    for (auto const& n : nodes) {
        std::erase_if(n->m_subsurfaces, [](const auto& e) { return e.expired(); });

        for (auto const& subsurfaceRef : n->m_subsurfaces) {
            const auto subsurface = subsurfaceRef.lock();
            if (!subsurface || !subsurface->added())
                continue;

            if (subsurface->m_zIndex < 0)
                continue;

            const auto surface = subsurface->m_surface.lock();
            if (!surface)
                continue;

            if (nodes2.empty())
                nodes2.reserve(nodes.size() * 2);

            nodes2.emplace_back(surface);
        }
    }

    if (!nodes2.empty())
        bfHelper(nodes2, fn, data);
}

void CWLSurfaceResource::breadthfirst(std::function<void(SP<CWLSurfaceResource>, const Vector2D&, void*)> fn, void* data) {
    const std::array surfs = {m_self.lock()};
    bfHelper(surfs, fn, data);
}

SP<CWLSurfaceResource> CWLSurfaceResource::findFirstPreorderHelper(SP<CWLSurfaceResource> root, std::function<bool(SP<CWLSurfaceResource>)> fn) {
    if (fn(root))
        return root;

    std::erase_if(root->m_subsurfaces, [](const auto& e) { return e.expired(); });

    for (auto const& subsurfaceRef : root->m_subsurfaces) {
        const auto subsurface = subsurfaceRef.lock();
        if (!subsurface || !subsurface->added())
            continue;

        const auto surface = subsurface->m_surface.lock();
        if (!surface)
            continue;

        const auto found = findFirstPreorderHelper(surface, fn);
        if (found)
            return found;
    }
    return nullptr;
}

SP<CWLSurfaceResource> CWLSurfaceResource::findFirstPreorder(std::function<bool(SP<CWLSurfaceResource>)> fn) {
    return findFirstPreorderHelper(m_self.lock(), fn);
}

SP<CWLSurfaceResource> CWLSurfaceResource::findWithCM() {
    return findFirstPreorder([this](SP<CWLSurfaceResource> surf) { return surf->m_colorManagement.valid() && surf->extends() == extends(); });
}

std::pair<SP<CWLSurfaceResource>, Vector2D> CWLSurfaceResource::at(const Vector2D& localCoords, bool allowsInput) {
    std::vector<std::pair<SP<CWLSurfaceResource>, Vector2D>> surfs;
    breadthfirst([&surfs](SP<CWLSurfaceResource> surf, const Vector2D& offset, void* data) { surfs.emplace_back(surf, offset); }, &surfs);

    for (auto const& [surf, pos] : surfs | std::views::reverse) {
        if (!allowsInput) {
            const auto BOX = CBox{pos, surf->m_current.size};
            if (BOX.containsPoint(localCoords))
                return {surf, localCoords - pos};
        } else {
            const auto REGION = surf->m_current.effectiveInputRegion().translate(pos);
            if (REGION.containsPoint(localCoords))
                return {surf, localCoords - pos};
        }
    }

    return {nullptr, {}};
}

uint32_t CWLSurfaceResource::id() {
    return wl_resource_get_id(m_resource->resource());
}

void CWLSurfaceResource::map() {
    if UNLIKELY (m_mapped)
        return;

    m_mapped = true;

    frame(Time::steadyNow());

    m_current.bufferDamage = CBox{{}, m_current.bufferSize};
    m_pending.bufferDamage = CBox{{}, m_pending.bufferSize};
}

void CWLSurfaceResource::unmap() {
    if UNLIKELY (!m_mapped)
        return;

    // unmapped content will never be displayed: terminate outstanding feedbacks,
    // or clients blocking on them (present_wait) stall forever.
    discardPresentationFeedbacks();

    m_mapped        = false;
    m_lastTransform = std::nullopt;
    m_lastScale     = std::nullopt;
    clearFifoBarrier(m_fifoBarrier.activeEpoch());

    // release the buffers.
    // this is necessary for XWayland to function correctly,
    // as it does not unmap via the traditional commit(null buffer) method, but via the X11 protocol.
    releaseBuffers();
}

void CWLSurfaceResource::releaseBuffers(bool onlyCurrent) {
    if (!onlyCurrent)
        dropPendingBuffer();
    dropCurrentBuffer();
}

void CWLSurfaceResource::error(int code, const std::string& str) {
    m_resource->error(code, str);
}

SP<CWlSurface> CWLSurfaceResource::getResource() {
    return m_resource;
}

CBox CWLSurfaceResource::extends() {
    CRegion full = CBox{{}, m_current.size};
    breadthfirst(
        [](SP<CWLSurfaceResource> surf, const Vector2D& offset, void* d) {
            if (surf->m_role->role() != SURFACE_ROLE_SUBSURFACE)
                return;

            sc<CRegion*>(d)->add(CBox{offset, surf->m_current.size});
        },
        &full);
    return full.getExtents();
}

void CWLSurfaceResource::scheduleUpdate(WP<CContentUpdate> update) {
    auto& state = update->state();
    if (state.buffer && state.buffer->isSynchronous()) {
        // synchronous (shm) buffers can be read immediately
        m_contentUpdates.clearConstraint(update, eContentUpdateConstraint::FENCE);
    } else if ((state.updated.bits.acquire || (state.buffer && !state.buffer->m_syncFds.empty())))
        refreshFenceConstraints();
    else
        // state commit without a buffer.
        m_contentUpdates.tryProcess();
}

void CWLSurfaceResource::refreshFenceConstraints() {
    m_contentUpdates.refreshFenceConstraints([this](CContentUpdate& update) { return refreshFenceConstraint(update); });
}

bool CWLSurfaceResource::refreshFenceConstraint(CContentUpdate& update) {
    auto& state        = update.state();
    auto  whenReadable = [surface = m_self] {
        g_pEventLoopManager->doLater([surface] {
            if (surface)
                surface->refreshFenceConstraints();
        });
    };

    if (state.updated.bits.acquire) {
        const auto SIGNALED = state.acquire.timeline()->check(state.acquire.point(), 0u);
        if (!SIGNALED.has_value() || *SIGNALED)
            return true;

        if (update.m_fenceWaiter)
            return update.m_fenceWaiter->failed;

        auto waiter = state.acquire.addWaiter(std::move(whenReadable));
        if (!waiter) {
            Log::logger->log(Log::ERR, "Failed to add explicit fence waiter in CWLSurfaceResource::refreshFenceConstraint");
            return true;
        }

        update.setFenceWaiter(std::move(waiter));
        return false;
    }

    if (!state.buffer)
        return true;

    if (update.m_fenceWaiter) {
        if (!update.m_fenceWaiter->fd.isReadable() && !update.m_fenceWaiter->failed)
            return false;
        update.cancelFenceWaiter();
    }

    auto& fds = state.buffer->m_syncFds;
    std::erase_if(fds, [](const auto& fd) { return fd.isReadable(); });
    if (fds.empty())
        return true;

    auto fd = std::move(fds.front());
    fds.erase(fds.begin());
    update.setFenceWaiter(g_pEventLoopManager->doOnReadable(std::move(fd), std::move(whenReadable)));
    return false;
}

void CWLSurfaceResource::applyUpdate(CContentUpdate& update, bool accumulateDamage) {
    auto& state = update.state();
    // only a new buffer supersedes the current, not yet presented content.
    if (state.updated.bits.buffer)
        PROTO::presentation->discardFeedbacks(m_current.presentationFeedbacks);

    auto lastTexture = m_current.texture;
    m_current.updateFrom(state, accumulateDamage);

    if (state.fifoBarrierEpoch != 0)
        activateFifoBarrier(state.fifoBarrierEpoch);

    if (m_current.buffer) {
        if (m_current.buffer->isSynchronous())
            m_current.updateSynchronousTexture(lastTexture);

        // if the surface is a cursor, update the shm buffer
        // TODO: don't update the entire texture
        if (m_role->role() == SURFACE_ROLE_CURSOR)
            updateCursorShm(m_current.accumulateBufferDamage());
    }

    if (m_current.texture)
        m_current.texture->m_transform = Math::wlTransformToHyprutils(m_current.transform);

    update.applyState();
}

void CWLSurfaceResource::publishUpdate() {
    m_events.commit.emit();

    // release the buffer if it's synchronous (SHM) as updateSynchronousTexture() has copied the buffer data to a GPU tex
    // if it doesn't have a role, we can't release it yet, in case it gets turned into a cursor.
    if (m_current.buffer && m_current.buffer->isSynchronous() && m_role->role() != SURFACE_ROLE_UNASSIGNED)
        dropCurrentBuffer();
}

void CWLSurfaceResource::publishSubsurfaceAdditions(std::vector<SP<CWLSurfaceResource>>& surfaces) {
    std::vector<SP<CWLSubsurfaceResource>> additions;

    for (const auto& subsurfaceRef : m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (!SUBSURFACE || !SUBSURFACE->m_added || SUBSURFACE->m_announced)
            continue;

        SUBSURFACE->m_announced = true;
        additions.emplace_back(SUBSURFACE);

        const auto SURFACE = SUBSURFACE->m_surface.lock();
        if (SURFACE && std::ranges::find(surfaces, SURFACE) == surfaces.end())
            surfaces.emplace_back(SURFACE);
    }

    for (const auto& subsurface : additions)
        m_events.newSubsurface.emit(subsurface);
}

bool CWLSurfaceResource::effectivelySynchronized() const {
    auto                                surface = m_self.lock();
    std::vector<SP<CWLSurfaceResource>> visited;

    while (surface && surface->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
        if (std::ranges::find(visited, surface) != visited.end())
            return false;

        visited.emplace_back(surface);

        const auto SUBSURFACE = sc<CSubsurfaceRole*>(surface->m_role.get())->m_subsurface.lock();
        if (!SUBSURFACE)
            return false;

        if (SUBSURFACE->m_sync)
            return true;

        surface = SUBSURFACE->m_parent.lock();
    }

    return false;
}

void CWLSurfaceResource::attachSynchronizedChildren(const WP<CContentUpdate>& update) {
    std::erase_if(m_subsurfaces, [](const auto& subsurface) { return !subsurface; });

    for (const auto& subsurfaceRef : m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (!SUBSURFACE)
            continue;

        const auto SURFACE = SUBSURFACE->m_surface.lock();
        if (!SURFACE)
            continue;

        SURFACE->m_contentUpdates.claimNewestSynchronized(update);
    }
}

void CWLSurfaceResource::prepareSubsurfaceState(CContentUpdate& update) {
    struct SSubsurfaceSnapshot {
        WP<CWLSubsurfaceResource> subsurface;
        Vector2D                  position;
        int                       zIndex = 0;
    };

    std::vector<SSubsurfaceSnapshot> snapshots;
    for (const auto& subsurfaceRef : m_subsurfaces) {
        const auto SUBSURFACE = subsurfaceRef.lock();
        if (!SUBSURFACE || !SUBSURFACE->m_pending.dirty)
            continue;

        snapshots.emplace_back(SUBSURFACE, SUBSURFACE->m_pending.position, SUBSURFACE->m_pending.zIndex);
        SUBSURFACE->m_pending.dirty = false;
    }

    if (snapshots.empty())
        return;

    update.addActivation([surface = m_self, snapshots = std::move(snapshots)] {
        bool changed = false;

        for (const auto& snapshot : snapshots) {
            const auto SUBSURFACE = snapshot.subsurface.lock();
            if (!SUBSURFACE)
                continue;

            SUBSURFACE->m_position = snapshot.position;
            SUBSURFACE->m_zIndex   = snapshot.zIndex;
            SUBSURFACE->m_added    = true;
            changed                = true;
        }

        if (changed && surface)
            surface->sortSubsurfaces();
    });
}

PImageDescription CWLSurfaceResource::getPreferredImageDescription() {
    static const auto PFORCE_HDR = CConfigValue<Config::INTEGER>("quirks:prefer_hdr");
    const auto        WINDOW     = m_hlSurface ? Desktop::View::CWindow::fromView(m_hlSurface->view()) : nullptr;

    if (*PFORCE_HDR == 1 || (*PFORCE_HDR == 2 && m_hlSurface && WINDOW && WINDOW->m_class == "gamescope"))
        return g_pCompositor->getHDRImageDescription();

    auto parent = m_self;
    if (parent->m_role->role() == SURFACE_ROLE_SUBSURFACE) {
        auto subsurface = sc<CSubsurfaceRole*>(parent->m_role.get())->m_subsurface.lock();
        parent          = subsurface->t1Parent();
    }
    PHLMONITORREF monitor;
    if (parent->m_enteredOutputs.size() == 1)
        monitor = parent->m_enteredOutputs[0];
    else if (m_hlSurface.valid() && WINDOW)
        monitor = WINDOW->m_monitor;

    return monitor ? monitor->m_imageDescription : g_pCompositor->getPreferredImageDescription();
}

void CWLSurfaceResource::sortSubsurfaces() {
    std::erase_if(m_subsurfaces, [](const auto& subsurface) { return !subsurface; });
    std::ranges::stable_sort(m_subsurfaces, [](const auto& a, const auto& b) {
        if (a->m_added != b->m_added)
            return a->m_added;

        return a->m_added ? a->m_zIndex < b->m_zIndex : a->m_pending.zIndex < b->m_pending.zIndex;
    });
}

bool CWLSurfaceResource::hasVisibleSubsurface() {
    for (auto const& subsurface : m_subsurfaces) {
        if (!subsurface || !subsurface->m_added || !subsurface->m_surface)
            continue;

        const auto& surf = subsurface->m_surface;
        if (surf->m_current.size.x > 0 && surf->m_current.size.y > 0)
            return true;
    }

    return false;
}

bool CWLSurfaceResource::isTearing() {
    if (m_enteredOutputs.empty() && m_hlSurface) {
        for (auto& m : State::monitorState()->monitors()) {
            if (!m || !m->m_enabled)
                continue;

            auto box = m_hlSurface->getSurfaceBoxGlobal();
            if (box && !box->intersection({m->m_position, m->m_size}).empty()) {
                if (m->m_tearingState.activelyTearing)
                    return true;
            }
        }
    } else {
        for (auto& m : m_enteredOutputs) {
            if (!m)
                continue;

            if (m->m_tearingState.activelyTearing)
                return true;
        }
    }
    return false;
}

void CWLSurfaceResource::prepareFifoState(CContentUpdate& update) {
    auto& state = update.state();
    if (state.waitBarrier) {
        const uint64_t QUEUED_EPOCH = m_contentUpdates.latestFifoBarrierEpoch();
        state.fifoWaitEpoch         = QUEUED_EPOCH != 0 ? QUEUED_EPOCH : m_fifoBarrier.activeEpoch();
        if (state.fifoWaitEpoch != 0 && update.mode() == eContentUpdateMode::DESYNCHRONIZED && NFifo::shouldLock(m_self.lock()))
            update.addConstraint(eContentUpdateConstraint::FIFO);
        else if (state.fifoWaitEpoch == 0) {
            static const auto PPEND = CConfigValue<Config::INTEGER>("debug:fifo_pending_workaround");
            if (*PPEND)
                scheduleFifoFrame();
        }
    }

    if (state.barrierSet)
        state.fifoBarrierEpoch = m_fifoBarrier.reserveEpoch();
}

void CWLSurfaceResource::activateFifoBarrier(uint64_t epoch) {
    if (epoch == 0)
        return;

    m_fifoBarrier.activate(epoch);
    if (!m_mapped) {
        m_fifoBarrier.clear(epoch);
        return;
    }

    constexpr auto EMERGENCY_TIMEOUT = std::chrono::seconds(1);
    if (!m_fifoEmergencyTimer) {
        m_fifoEmergencyTimer = makeShared<CEventLoopTimer>(
            EMERGENCY_TIMEOUT,
            [surface = m_self](SP<CEventLoopTimer> self, void*) {
                if (self)
                    self->updateTimeout(std::nullopt);
                if (surface)
                    surface->clearFifoBarrier(surface->fifoBarrierEpoch());
            },
            nullptr);
        g_pEventLoopManager->addTimer(m_fifoEmergencyTimer);
    } else
        m_fifoEmergencyTimer->updateTimeout(EMERGENCY_TIMEOUT);

    scheduleFifoFrame();
}

uint64_t CWLSurfaceResource::fifoBarrierEpoch() const {
    return m_fifoBarrier.activeEpoch();
}

void CWLSurfaceResource::stageFifoLatch(PHLMONITOR monitor, bool discarded) {
    const uint64_t EPOCH = m_fifoBarrier.activeEpoch();
    if (EPOCH == 0)
        return;

    if (discarded) {
        clearFifoBarrier(EPOCH);
        return;
    }

    if (!monitor || monitor->m_tearingState.activelyTearing)
        return;

    monitor->stageFifoLatch(m_self, EPOCH);
}

void CWLSurfaceResource::clearFifoBarrier(uint64_t epoch) {
    if (!m_fifoBarrier.clear(epoch))
        return;

    if (m_fifoEmergencyTimer)
        m_fifoEmergencyTimer->updateTimeout(std::nullopt);

    m_contentUpdates.clearFifoEpoch(epoch);
    if (m_mapped)
        scheduleFifoFrame();
}

void CWLSurfaceResource::scheduleFifoFrame() {
    auto schedule = [](PHLMONITOR monitor) {
        if (monitor && monitor->m_enabled && !monitor->m_tearingState.activelyTearing)
            monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_NEEDS_FRAME);
    };

    if (!m_enteredOutputs.empty()) {
        for (const auto& monitor : m_enteredOutputs)
            schedule(monitor.lock());
        return;
    }

    if (!m_hlSurface)
        return;

    const auto BOX = m_hlSurface->getSurfaceBoxGlobal();
    if (!BOX)
        return;

    for (const auto& monitor : State::monitorState()->monitors()) {
        if (monitor && !BOX->intersection({monitor->m_position, monitor->m_size}).empty())
            schedule(monitor);
    }
}

void CWLSurfaceResource::updateCursorShm(CRegion damage) {
    if (damage.empty())
        return;

    auto buf = m_current.buffer ? m_current.buffer : SP<IHLBuffer>{};

    if UNLIKELY (!buf)
        return;

    auto& shmData  = CCursorSurfaceRole::cursorPixelData(m_self.lock());
    auto  shmAttrs = buf->shm();

    if (!shmAttrs.success) {
        LOGM(Log::TRACE, "updateCursorShm: ignoring, not a shm buffer");
        return;
    }

    damage.intersect(CBox{0, 0, buf->size.x, buf->size.y});

    // no need to end, shm.
    auto [pixelData, fmt, bufLen] = buf->beginDataPtr(0);

    shmData.resize(bufLen);

    int         rectsNum = 0;
    const auto* rects    = pixman_region32_rectangles(damage.pixman(), &rectsNum);

    if (rectsNum == 1 && rects[0].x2 == buf->size.x && rects[0].y2 == buf->size.y)
        memcpy(shmData.data(), pixelData, bufLen);
    else {
        const auto stride = shmAttrs.stride;
        damage.forEachRect([&pixelData, &shmData, stride](const auto& box) {
            for (auto y = box.y1; y < box.y2; ++y) {
                // bpp is 32 INSALLAH
                auto begin = y * stride + 4 * box.x1;
                auto len   = 4 * (box.x2 - box.x1);
                memcpy(shmData.data() + begin, pixelData + begin, len);
            }
        });
    }
}

void CWLSurfaceResource::presentFeedback(const Time::steady_tp& when, PHLMONITOR pMonitor, bool discarded) {
    frame(when);
    stageFifoLatch(pMonitor, discarded);

    // if it's empty then CPresentationProtocol::m_feedbacks doesn't contain any feedback listeners for this surface and frame
    if (m_current.presentationFeedbacks.empty())
        return;

    // discarded content will never be scanned out, so there is no present event coming.
    if (discarded) {
        PROTO::presentation->discardFeedbacks(m_current.presentationFeedbacks);
        return;
    }

    auto FEEDBACK = makeUnique<CQueuedPresentationData>(m_self.lock(), std::move(m_current.presentationFeedbacks));
    FEEDBACK->attachMonitor(pMonitor);
    FEEDBACK->presented();
    if (!pMonitor->m_lastScanout.expired()) {
        const auto WINDOW = m_hlSurface ? Desktop::View::CWindow::fromView(m_hlSurface->view()) : nullptr;
        if (WINDOW == pMonitor->m_lastScanout)
            FEEDBACK->setPresentationType(true);
    }
    PROTO::presentation->queueData(std::move(FEEDBACK));
}

CWLCompositorResource::CWLCompositorResource(SP<CWlCompositor> resource_) : m_resource(resource_) {
    if UNLIKELY (!good())
        return;

    m_resource->setOnDestroy([this](CWlCompositor* r) { PROTO::compositor->destroyResource(this); });

    m_resource->setCreateSurface([](CWlCompositor* r, uint32_t id) {
        const auto RESOURCE = PROTO::compositor->m_surfaces.emplace_back(makeShared<CWLSurfaceResource>(makeShared<CWlSurface>(r->client(), r->version(), id)));

        if UNLIKELY (!RESOURCE->good()) {
            r->noMemory();
            PROTO::compositor->m_surfaces.pop_back();
            return;
        }

        RESOURCE->m_self           = RESOURCE;
        RESOURCE->m_contentUpdates = CContentUpdateQueue(RESOURCE);

        LOGM(Log::DEBUG, "New wl_surface with id {} at {:x}", id, (uintptr_t)RESOURCE.get());

        PROTO::compositor->m_events.newSurface.emit(RESOURCE);
    });

    m_resource->setCreateRegion([](CWlCompositor* r, uint32_t id) {
        const auto RESOURCE = PROTO::compositor->m_regions.emplace_back(makeShared<CWLRegionResource>(makeShared<CWlRegion>(r->client(), r->version(), id)));

        if UNLIKELY (!RESOURCE->good()) {
            r->noMemory();
            PROTO::compositor->m_regions.pop_back();
            return;
        }

        RESOURCE->m_self = RESOURCE;

        LOGM(Log::DEBUG, "New wl_region with id {} at {:x}", id, (uintptr_t)RESOURCE.get());
    });
}

bool CWLCompositorResource::good() {
    return m_resource->resource();
}

CWLCompositorProtocol::CWLCompositorProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CWLCompositorProtocol::registerContentUpdateCandidate(WP<CContentUpdate> update) {
    if (!update || std::ranges::find(m_contentUpdateCandidates, update) != m_contentUpdateCandidates.end())
        return;

    m_contentUpdateCandidates.emplace_back(std::move(update));
}

bool CWLCompositorProtocol::collectContentUpdateGraph(const WP<CContentUpdate>& update, std::vector<WP<CContentUpdate>>& graph, std::vector<WP<CContentUpdate>>& visiting,
                                                      eContentUpdateConstraint ignoredConstraints) {
    if (!update || update->m_applied || std::ranges::find(graph, update) != graph.end())
        return true;

    if (!update->finalized() || !update->readyIgnoring(ignoredConstraints))
        return false;

    if (std::ranges::find(visiting, update) != visiting.end()) {
        Log::logger->log(Log::ERR, "Content Update dependency cycle detected");
        return false;
    }

    visiting.emplace_back(update);

    if (!collectContentUpdateGraph(update->m_previous, graph, visiting, ignoredConstraints)) {
        visiting.pop_back();
        return false;
    }

    for (const auto& dependency : update->m_dependencies) {
        if (collectContentUpdateGraph(dependency, graph, visiting, ignoredConstraints))
            continue;

        visiting.pop_back();
        return false;
    }

    visiting.pop_back();
    graph.emplace_back(update);
    return true;
}

std::optional<Time::steady_tp> CWLCompositorProtocol::effectiveContentUpdateTarget(const WP<CContentUpdate>& update) {
    if (!update || !update->finalized())
        return std::nullopt;

    for (const auto& candidate : m_contentUpdateCandidates | std::views::reverse) {
        if (!candidate)
            continue;

        const auto SURFACE = candidate->m_surface.lock();
        if (!SURFACE || !SURFACE->m_contentUpdates.isCandidate(candidate))
            continue;

        std::vector<WP<CContentUpdate>> graph;
        std::vector<WP<CContentUpdate>> visiting;
        if (!collectContentUpdateGraph(candidate, graph, visiting, eContentUpdateConstraint::TIMER) || std::ranges::find(graph, update) == graph.end())
            continue;

        return contentUpdateTarget(graph);
    }

    return std::nullopt;
}

std::optional<Time::steady_tp> CWLCompositorProtocol::contentUpdateTarget(const std::vector<WP<CContentUpdate>>& graph) {
    std::optional<Time::steady_tp> target;
    for (const auto& update : graph) {
        if (!update)
            continue;

        const auto& updateTarget = update->state().commitTimingTarget;
        if (updateTarget && (!target || *updateTarget > *target))
            target = updateTarget;
    }

    return target;
}

bool CWLCompositorProtocol::applyContentUpdateGraph(const std::vector<WP<CContentUpdate>>& graph) {
    std::vector<SP<CWLSurfaceResource>> surfaces;
    surfaces.reserve(graph.size());

    for (const auto& update : graph) {
        if (!update || update->m_applied)
            continue;

        const auto SURFACE = update->m_surface.lock();
        if (!SURFACE)
            return false;

        if (std::ranges::find(surfaces, SURFACE) == surfaces.end())
            surfaces.emplace_back(SURFACE);
    }

    std::vector<SP<CWLSurfaceResource>> appliedSurfaces;
    appliedSurfaces.reserve(surfaces.size());

    for (const auto& update : graph) {
        if (!update || update->m_applied)
            continue;

        const auto SURFACE = update->m_surface.lock();
        ASSERT(SURFACE);
        const bool ACCUMULATE_DAMAGE = std::ranges::find(appliedSurfaces, SURFACE) != appliedSurfaces.end();
        SURFACE->applyUpdate(*update, ACCUMULATE_DAMAGE);
        if (!ACCUMULATE_DAMAGE)
            appliedSurfaces.emplace_back(SURFACE);
        update->m_applied = true;
    }

    for (const auto& surface : surfaces)
        surface->m_contentUpdates.removeApplied();

    for (const auto& surface : surfaces)
        surface->m_contentUpdates.registerCandidates();

    std::ranges::stable_sort(surfaces, {}, subsurfaceDepth);
    for (size_t i = 0; i < surfaces.size(); ++i)
        surfaces[i]->publishSubsurfaceAdditions(surfaces);

    std::ranges::stable_sort(surfaces, {}, subsurfaceDepth);
    for (const auto& surface : surfaces)
        surface->publishUpdate();

    return !surfaces.empty();
}

void CWLCompositorProtocol::processContentUpdates() {
    if (m_processingContentUpdates) {
        m_processContentUpdatesAgain = true;
        return;
    }

    m_processingContentUpdates = true;

    bool progressed = false;
    do {
        progressed                   = false;
        m_processContentUpdatesAgain = false;

        std::erase_if(m_contentUpdateCandidates, [](const auto& candidate) {
            if (!candidate)
                return true;

            const auto SURFACE = candidate->m_surface.lock();
            return !SURFACE || !SURFACE->m_contentUpdates.isCandidate(candidate);
        });

        const auto candidates = m_contentUpdateCandidates;
        for (const auto& candidate : candidates | std::views::reverse) {
            if (!candidate)
                continue;

            const auto SURFACE = candidate->m_surface.lock();
            if (!SURFACE || !SURFACE->m_contentUpdates.isCandidate(candidate))
                continue;

            std::vector<WP<CContentUpdate>> graph;
            std::vector<WP<CContentUpdate>> visiting;
            if (!collectContentUpdateGraph(candidate, graph, visiting))
                continue;

            if (applyContentUpdateGraph(graph)) {
                progressed = true;
                break;
            }
        }
    } while (progressed || m_processContentUpdatesAgain);

    m_processingContentUpdates = false;
}

void CWLCompositorProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeShared<CWLCompositorResource>(makeShared<CWlCompositor>(client, ver, id)));

    if UNLIKELY (!RESOURCE->good()) {
        wl_client_post_no_memory(client);
        m_managers.pop_back();
        return;
    }
}

void CWLCompositorProtocol::destroyResource(CWLCompositorResource* resource) {
    std::erase_if(m_managers, [&](const auto& other) { return other.get() == resource; });
}

void CWLCompositorProtocol::destroyResource(CWLSurfaceResource* resource) {
    std::erase_if(m_surfaces, [&](const auto& other) { return other.get() == resource; });
    processContentUpdates();
}

void CWLCompositorProtocol::destroyResource(CWLRegionResource* resource) {
    std::erase_if(m_regions, [&](const auto& other) { return other.get() == resource; });
}

void CWLCompositorProtocol::forEachSurface(std::function<void(SP<CWLSurfaceResource>)> fn) {
    const auto surfaces = m_surfaces;

    for (auto& surf : surfaces) {
        if (!surf)
            continue;

        fn(surf);
    }
}
