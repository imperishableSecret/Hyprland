#include "ContentUpdate.hpp"

#include "../PresentationTime.hpp"
#include "../core/Compositor.hpp"

#include <algorithm>

eContentUpdateConstraint operator|(eContentUpdateConstraint lhs, eContentUpdateConstraint rhs) {
    return sc<eContentUpdateConstraint>(sc<uint8_t>(lhs) | sc<uint8_t>(rhs));
}

eContentUpdateConstraint operator&(eContentUpdateConstraint lhs, eContentUpdateConstraint rhs) {
    return sc<eContentUpdateConstraint>(sc<uint8_t>(lhs) & sc<uint8_t>(rhs));
}

eContentUpdateConstraint& operator|=(eContentUpdateConstraint& lhs, eContentUpdateConstraint rhs) {
    lhs = lhs | rhs;
    return lhs;
}

eContentUpdateConstraint& operator&=(eContentUpdateConstraint& lhs, eContentUpdateConstraint rhs) {
    lhs = lhs & rhs;
    return lhs;
}

eContentUpdateConstraint operator~(eContentUpdateConstraint constraint) {
    return sc<eContentUpdateConstraint>(~sc<uint8_t>(constraint));
}

CContentUpdate::CContentUpdate(const SSurfaceState& state, WP<CWLSurfaceResource> surface) : m_state(state), m_surface(std::move(surface)) {}

SSurfaceState& CContentUpdate::state() {
    return m_state;
}

const SSurfaceState& CContentUpdate::state() const {
    return m_state;
}

WP<CWLSurfaceResource> CContentUpdate::surface() const {
    return m_surface;
}

void CContentUpdate::addConstraint(eContentUpdateConstraint constraint) {
    ASSERT(!m_finalized);
    ASSERT(constraint != eContentUpdateConstraint::NONE);
    m_constraints |= constraint;
}

void CContentUpdate::clearConstraint(eContentUpdateConstraint constraint) {
    ASSERT(constraint != eContentUpdateConstraint::NONE);
    m_constraints &= ~constraint;
}

void CContentUpdate::addActivation(std::move_only_function<void()>&& activation) {
    ASSERT(!m_finalized);
    m_activations.emplace_back(std::move(activation));
}

bool CContentUpdate::ready() const {
    return m_constraints == eContentUpdateConstraint::NONE;
}

bool CContentUpdate::finalized() const {
    return m_finalized;
}

void CContentUpdate::finalize() {
    m_finalized = true;
}

void CContentUpdate::applyState() {
    for (auto& activation : m_activations)
        activation();
}

CContentUpdateQueue::CContentUpdateQueue(WP<CWLSurfaceResource> surface) : m_surface(std::move(surface)) {}

void CContentUpdateQueue::clear() {
    for (const auto& update : m_queue)
        PROTO::presentation->discardFeedbacks(update->state().presentationFeedbacks);

    m_queue.clear();
}

WP<CContentUpdate> CContentUpdateQueue::enqueue(UP<CContentUpdate>&& update) {
    return m_queue.emplace_back(std::move(update));
}

void CContentUpdateQueue::drop(const WP<CContentUpdate>& update) {
    const auto IT = find(update);
    if (IT == m_queue.end())
        return;

    PROTO::presentation->discardFeedbacks((*IT)->state().presentationFeedbacks);
    m_queue.erase(IT);
}

void CContentUpdateQueue::addConstraint(const WP<CContentUpdate>& update, eContentUpdateConstraint constraint) {
    const auto IT = find(update);
    if (IT != m_queue.end())
        (*IT)->addConstraint(constraint);
}

void CContentUpdateQueue::clearConstraint(const WP<CContentUpdate>& update, eContentUpdateConstraint constraint) {
    const auto IT = find(update);
    if (IT == m_queue.end())
        return;

    (*IT)->clearConstraint(constraint);
    tryProcess();
}

void CContentUpdateQueue::clearFirstConstraints(eContentUpdateConstraint constraints) {
    ASSERT(constraints != eContentUpdateConstraint::NONE);
    for (const auto& update : m_queue) {
        if ((update->m_constraints & constraints) == eContentUpdateConstraint::NONE)
            continue;

        update->m_constraints &= ~constraints;
        break;
    }

    tryProcess();
}

void CContentUpdateQueue::clearFifoEpoch(uint64_t epoch) {
    if (epoch == 0)
        return;

    for (const auto& update : m_queue) {
        if (update->state().fifoWaitEpoch == epoch)
            update->clearConstraint(eContentUpdateConstraint::FIFO);
    }

    tryProcess();
}

uint64_t CContentUpdateQueue::latestFifoBarrierEpoch() const {
    const auto UPDATE = std::ranges::find_if(m_queue.rbegin(), m_queue.rend(), [](const auto& update) { return update->state().fifoBarrierEpoch != 0; });
    return UPDATE == m_queue.rend() ? 0 : (*UPDATE)->state().fifoBarrierEpoch;
}

void CContentUpdateQueue::finalize(const WP<CContentUpdate>& update) {
    const auto IT = find(update);
    if (IT == m_queue.end())
        return;

    (*IT)->finalize();
    tryProcess();
}

auto CContentUpdateQueue::find(const WP<CContentUpdate>& update) -> std::deque<UP<CContentUpdate>>::iterator {
    if (!update)
        return m_queue.end();

    return std::ranges::find_if(m_queue, [&update](const auto& queued) { return queued.get() == update.get(); });
}

void CContentUpdateQueue::tryProcess() {
    while (!m_queue.empty()) {
        auto& update = m_queue.front();
        if ((update->m_constraints & eContentUpdateConstraint::FIFO) != eContentUpdateConstraint::NONE && !m_surface->fifoBarrierMatches(update->state().fifoWaitEpoch))
            update->clearConstraint(eContentUpdateConstraint::FIFO);

        if (!update->finalized() || !update->ready())
            return;

        m_surface->applyUpdate(*update);
        m_surface->publishUpdate();
        m_queue.pop_front();
    }
}
