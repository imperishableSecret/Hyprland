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

CContentUpdate::CContentUpdate(const SSurfaceState& state, WP<CWLSurfaceResource> surface, eContentUpdateMode mode) : m_state(state), m_surface(std::move(surface)), m_mode(mode) {}

SSurfaceState& CContentUpdate::state() {
    return m_state;
}

const SSurfaceState& CContentUpdate::state() const {
    return m_state;
}

WP<CWLSurfaceResource> CContentUpdate::surface() const {
    return m_surface;
}

eContentUpdateMode CContentUpdate::mode() const {
    return m_mode;
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

bool CContentUpdate::addDependency(WP<CContentUpdate> dependency) {
    ASSERT(!m_finalized);
    if (!dependency || dependency.get() == this || std::ranges::find(m_dependencies, dependency) != m_dependencies.end())
        return false;

    m_dependencies.emplace_back(std::move(dependency));
    return true;
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

void CContentUpdate::removeDependency(const WP<CContentUpdate>& dependency) {
    std::erase_if(m_dependencies, [&dependency](const auto& candidate) { return !candidate || candidate == dependency; });
}

CContentUpdateQueue::CContentUpdateQueue(WP<CWLSurfaceResource> surface) : m_surface(std::move(surface)) {}

void CContentUpdateQueue::clear() {
    if (PROTO::presentation) {
        for (const auto& update : m_queue)
            PROTO::presentation->discardFeedbacks(update->state().presentationFeedbacks);
    }

    m_queue.clear();
}

WP<CContentUpdate> CContentUpdateQueue::enqueue(UP<CContentUpdate>&& update) {
    if (!m_queue.empty())
        update->m_previous = WP<CContentUpdate>{m_queue.back()};

    return m_queue.emplace_back(std::move(update));
}

void CContentUpdateQueue::drop(const WP<CContentUpdate>& update) {
    const auto IT = find(update);
    if (IT == m_queue.end())
        return;

    PROTO::presentation->discardFeedbacks((*IT)->state().presentationFeedbacks);
    m_queue.erase(IT);
    tryProcess();
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

WP<CContentUpdate> CContentUpdateQueue::newestUnclaimedSynchronized() const {
    if (m_queue.empty() || m_queue.back()->m_mode != eContentUpdateMode::SYNCHRONIZED || m_queue.back()->m_claimedBy)
        return {};

    return WP<CContentUpdate>{m_queue.back()};
}

void CContentUpdateQueue::claimNewestSynchronized(const WP<CContentUpdate>& dependent) {
    if (!dependent)
        return;

    const auto DEPENDENCY = newestUnclaimedSynchronized();
    if (!DEPENDENCY || !dependent->addDependency(DEPENDENCY))
        return;

    DEPENDENCY->m_claimedBy = dependent;
}

void CContentUpdateQueue::releaseSynchronizedUpdates() {
    for (const auto& update : m_queue)
        convertToDesynchronized(WP<CContentUpdate>{update});

    registerCandidates();
}

void CContentUpdateQueue::convertUnreachableSynchronizedUpdates() {
    std::vector<bool> reachable;
    reachable.reserve(m_queue.size());

    for (const auto& update : m_queue) {
        std::vector<WP<CContentUpdate>> visiting;
        reachable.emplace_back(reachableFromDesynchronized(WP<CContentUpdate>{update}, visiting));
    }

    for (size_t i = 0; i < m_queue.size(); ++i) {
        auto& update = m_queue[i];
        if (update->m_mode != eContentUpdateMode::SYNCHRONIZED || reachable[i])
            continue;

        convertToDesynchronized(WP<CContentUpdate>{update});
    }

    registerCandidates();
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

auto CContentUpdateQueue::find(const WP<CContentUpdate>& update) const -> std::deque<UP<CContentUpdate>>::const_iterator {
    if (!update)
        return m_queue.end();

    return std::ranges::find_if(m_queue, [&update](const auto& queued) { return queued.get() == update.get(); });
}

void CContentUpdateQueue::convertToDesynchronized(const WP<CContentUpdate>& update) {
    if (!update || update->m_mode != eContentUpdateMode::SYNCHRONIZED)
        return;

    if (update->m_claimedBy)
        update->m_claimedBy->removeDependency(update);

    update->m_claimedBy.reset();
    update->m_mode = eContentUpdateMode::DESYNCHRONIZED;
}

bool CContentUpdateQueue::isCandidate(const WP<CContentUpdate>& update) const {
    const auto IT = find(update);
    if (IT == m_queue.end() || (*IT)->m_mode != eContentUpdateMode::DESYNCHRONIZED)
        return false;

    return std::ranges::none_of(m_queue.begin(), IT, [](const auto& queued) { return queued->m_mode == eContentUpdateMode::SYNCHRONIZED; });
}

bool CContentUpdateQueue::reachableFromDesynchronized(const WP<CContentUpdate>& update, std::vector<WP<CContentUpdate>>& visiting) const {
    if (!update)
        return false;

    if (update->m_mode == eContentUpdateMode::DESYNCHRONIZED)
        return true;

    if (std::ranges::find(visiting, update) != visiting.end())
        return false;

    visiting.emplace_back(update);

    if (update->m_claimedBy) {
        const auto SURFACE = update->m_claimedBy->m_surface.lock();
        if (SURFACE && SURFACE->m_contentUpdates.reachableFromDesynchronized(update->m_claimedBy, visiting)) {
            visiting.pop_back();
            return true;
        }
    }

    const auto IT = find(update);
    if (IT != m_queue.end()) {
        const auto NEXT = std::next(IT);
        if (NEXT != m_queue.end() && (*NEXT)->m_previous == update && reachableFromDesynchronized(WP<CContentUpdate>{*NEXT}, visiting)) {
            visiting.pop_back();
            return true;
        }
    }

    visiting.pop_back();
    return false;
}

void CContentUpdateQueue::removeApplied() {
    std::erase_if(m_queue, [](const auto& update) { return update->m_applied; });
}

void CContentUpdateQueue::registerCandidates() {
    if (!PROTO::compositor)
        return;

    for (const auto& update : m_queue) {
        if (update->m_mode == eContentUpdateMode::SYNCHRONIZED)
            break;

        PROTO::compositor->registerContentUpdateCandidate(WP<CContentUpdate>{update});
    }
}

void CContentUpdateQueue::tryProcess() {
    registerCandidates();
    PROTO::compositor->processContentUpdates();
}
