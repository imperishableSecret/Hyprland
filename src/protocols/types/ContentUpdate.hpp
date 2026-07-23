#pragma once

#include "../../helpers/memory/Memory.hpp"
#include "SurfaceState.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

class CWLSurfaceResource;
class CWLCompositorProtocol;
struct SEventLoopReadableWaiter;

enum class eContentUpdateMode : uint8_t {
    SYNCHRONIZED,
    DESYNCHRONIZED,
};

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
    CContentUpdate(const SSurfaceState& state, WP<CWLSurfaceResource> surface, eContentUpdateMode mode = eContentUpdateMode::DESYNCHRONIZED);
    ~CContentUpdate();

    SSurfaceState&         state();
    const SSurfaceState&   state() const;
    WP<CWLSurfaceResource> surface() const;
    eContentUpdateMode     mode() const;
    void                   addConstraint(eContentUpdateConstraint constraint);
    void                   clearConstraint(eContentUpdateConstraint constraint);
    void                   addActivation(std::move_only_function<void()>&& activation);
    bool                   ready() const;
    bool                   finalized() const;

  private:
    SSurfaceState                                m_state;
    WP<CWLSurfaceResource>                       m_surface;
    WP<CContentUpdate>                           m_previous;
    std::vector<WP<CContentUpdate>>              m_dependencies;
    WP<CContentUpdate>                           m_claimedBy;
    eContentUpdateMode                           m_mode        = eContentUpdateMode::DESYNCHRONIZED;
    eContentUpdateConstraint                     m_constraints = eContentUpdateConstraint::NONE;
    std::vector<std::move_only_function<void()>> m_activations;
    WP<SEventLoopReadableWaiter>                 m_fenceWaiter;
    bool                                         m_finalized = false;
    bool                                         m_applied   = false;

    void                                         finalize();
    void                                         applyState();
    void                                         setFenceWaiter(WP<SEventLoopReadableWaiter> waiter);
    void                                         cancelFenceWaiter();
    bool                                         readyIgnoring(eContentUpdateConstraint constraints) const;
    bool                                         addDependency(WP<CContentUpdate> dependency);
    void                                         removeDependency(const WP<CContentUpdate>& dependency);

    friend class CContentUpdateQueue;
    friend class CContentUpdateTestAccess;
    friend class CWLCompositorProtocol;
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
    void               claimNewestSynchronized(const WP<CContentUpdate>& dependent);
    void               releaseSynchronizedUpdates();
    void               convertUnreachableSynchronizedUpdates();
    void               finalize(const WP<CContentUpdate>& update);
    void               tryProcess();

  private:
    std::deque<UP<CContentUpdate>>                          m_queue;
    WP<CWLSurfaceResource>                                  m_surface;

    typename std::deque<UP<CContentUpdate>>::iterator       find(const WP<CContentUpdate>& update);
    typename std::deque<UP<CContentUpdate>>::const_iterator find(const WP<CContentUpdate>& update) const;
    WP<CContentUpdate>                                      newestUnclaimedSynchronized() const;
    void                                                    convertToDesynchronized(const WP<CContentUpdate>& update);
    bool                                                    isCandidate(const WP<CContentUpdate>& update) const;
    bool                                                    reachableFromDesynchronized(const WP<CContentUpdate>& update, std::vector<WP<CContentUpdate>>& visiting) const;
    void                                                    refreshFenceConstraints(const std::function<bool(CContentUpdate&)>& ready);
    void                                                    removeApplied();
    void                                                    registerCandidates();

    friend class CWLCompositorProtocol;
    friend class CWLSurfaceResource;
    friend class CContentUpdateTestAccess;
};
