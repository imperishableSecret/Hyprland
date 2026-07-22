#pragma once

#include "../../helpers/memory/Memory.hpp"
#include "SurfaceState.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

class CWLSurfaceResource;

enum class eContentUpdateConstraint : uint8_t {
    NONE  = 0,
    FENCE = 1 << 0,
    FIFO  = 1 << 1,
    TIMER = 1 << 2,
};

eContentUpdateConstraint  operator|(eContentUpdateConstraint lhs, eContentUpdateConstraint rhs);
eContentUpdateConstraint  operator&(eContentUpdateConstraint lhs, eContentUpdateConstraint rhs);
eContentUpdateConstraint& operator|=(eContentUpdateConstraint& lhs, eContentUpdateConstraint rhs);
eContentUpdateConstraint& operator&=(eContentUpdateConstraint& lhs, eContentUpdateConstraint rhs);
eContentUpdateConstraint  operator~(eContentUpdateConstraint constraint);

// Commit-owned state snapshot with mutable readiness and lifecycle bookkeeping.
class CContentUpdate {
  public:
    CContentUpdate(const SSurfaceState& state, WP<CWLSurfaceResource> surface);

    SSurfaceState&         state();
    const SSurfaceState&   state() const;
    WP<CWLSurfaceResource> surface() const;
    void                   addConstraint(eContentUpdateConstraint constraint);
    void                   clearConstraint(eContentUpdateConstraint constraint);
    void                   addActivation(std::move_only_function<void()>&& activation);
    bool                   ready() const;
    bool                   finalized() const;

  private:
    SSurfaceState                                m_state;
    WP<CWLSurfaceResource>                       m_surface;
    eContentUpdateConstraint                     m_constraints = eContentUpdateConstraint::NONE;
    std::vector<std::move_only_function<void()>> m_activations;
    bool                                         m_finalized = false;

    void                                         finalize();
    void                                         applyState();

    friend class CContentUpdateQueue;
    friend class CContentUpdateTestAccess;
    friend class CWLSurfaceResource;
};

class CContentUpdateQueue {
  public:
    CContentUpdateQueue() = default;
    explicit CContentUpdateQueue(WP<CWLSurfaceResource> surface);

    void               clear();
    WP<CContentUpdate> enqueue(UP<CContentUpdate>&& update);
    void               drop(const WP<CContentUpdate>& update);
    void               addConstraint(const WP<CContentUpdate>& update, eContentUpdateConstraint constraint);
    void               clearConstraint(const WP<CContentUpdate>& update, eContentUpdateConstraint constraint);
    void               clearFirstConstraints(eContentUpdateConstraint constraints);
    void               clearFifoEpoch(uint64_t epoch);
    uint64_t           latestFifoBarrierEpoch() const;
    void               finalize(const WP<CContentUpdate>& update);
    void               tryProcess();

  private:
    std::deque<UP<CContentUpdate>>                    m_queue;
    WP<CWLSurfaceResource>                            m_surface;

    typename std::deque<UP<CContentUpdate>>::iterator find(const WP<CContentUpdate>& update);
};
