#include "SurfaceStateQueue.hpp"
#include "../core/Compositor.hpp"
#include "SurfaceState.hpp"

CSurfaceStateQueue::CSurfaceStateQueue(WP<CWLSurfaceResource> surf) : m_surface(std::move(surf)) {}

void CSurfaceStateQueue::clear() {
    while (!m_queue.empty()) {
        const WP<SSurfaceState> STATE = m_queue.front();
        if (m_surface)
            m_surface->m_events.stateDiscarded.emit(STATE);
        m_queue.pop_front();
    }
}

WP<SSurfaceState> CSurfaceStateQueue::enqueue(UP<SSurfaceState>&& state) {
    return m_queue.emplace_back(std::move(state));
}

WP<SSurfaceState> CSurfaceStateQueue::latestFifoBarrier() const {
    for (auto state = m_queue.rbegin(); state != m_queue.rend(); ++state) {
        if ((*state)->barrierSet && (*state)->fifoBarrierOwner)
            return WP<SSurfaceState>{*state};
    }

    return {};
}

void CSurfaceStateQueue::dropState(const WP<SSurfaceState>& state) {
    auto it = find(state);
    if (it == m_queue.end())
        return;

    if (m_surface)
        m_surface->m_events.stateDiscarded.emit(*it);
    m_queue.erase(it);
}

void CSurfaceStateQueue::lock(const WP<SSurfaceState>& weakState, eLockReason reason) {
    ASSERT(reason != LOCK_REASON_NONE);
    auto it = find(weakState);
    if (it == m_queue.end())
        return;

    it->get()->lockMask |= reason;
}

void CSurfaceStateQueue::unlock(const WP<SSurfaceState>& state, eLockReason reason) {
    ASSERT(reason != LOCK_REASON_NONE);
    auto it = find(state);
    if (it == m_queue.end())
        return;

    it->get()->lockMask &= ~reason;
    tryProcess();
}

void CSurfaceStateQueue::unlockFirst(eLockReason reason) {
    ASSERT(reason != LOCK_REASON_NONE);
    for (auto& it : m_queue) {
        if ((it->lockMask & reason) != LOCK_REASON_NONE) {
            it->lockMask &= ~reason;
            break;
        }
    }

    tryProcess();
}

auto CSurfaceStateQueue::find(const WP<SSurfaceState>& state) -> std::deque<UP<SSurfaceState>>::iterator {
    if (state.expired())
        return m_queue.end();

    auto* raw = state.get(); // get raw pointer

    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
        if (it->get() == raw)
            return it;
    }

    return m_queue.end();
}

void CSurfaceStateQueue::tryProcess() {
    const auto SURFACE = m_surface.lock();
    if (!SURFACE)
        return;

    while (!m_queue.empty()) {
        auto& front = m_queue.front();
        if (front->lockMask != LOCK_REASON_NONE)
            return;

        SURFACE->commitState(*front);
        SURFACE->m_events.stateApplied.emit(front);
        m_queue.pop_front();
    }
}
