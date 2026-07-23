#pragma once

#include "../../helpers/math/Math.hpp"
#include "../../helpers/time/Time.hpp"
#include "../../managers/eventLoop/EventLoopTimer.hpp"
#include "../WaylandProtocol.hpp"
#include "./Buffer.hpp"

namespace Render {
    class ITexture;
}
class CDRMSyncPointState;
class CWLCallbackResource;
class CPresentationFeedback;

class CFifoBarrierCondition {
  public:
    uint64_t reserveEpoch();
    void     activate(uint64_t epoch);
    bool     clear(uint64_t epoch);
    bool     matches(uint64_t epoch) const;
    uint64_t activeEpoch() const;

  private:
    uint64_t m_activeEpoch = 0;
    uint64_t m_nextEpoch   = 1;

    friend class CFifoBarrierConditionTestAccess;
};

struct SSurfaceState {
    union {
        uint16_t all = 0;
        struct {
            bool buffer : 1;
            bool damage : 1;
            bool opaque : 1;
            bool input : 1;
            bool transform : 1;
            bool scale : 1;
            bool offset : 1;
            bool viewport : 1;
            bool acquire : 1;
            bool acked : 1;
            bool frame : 1;
            bool fifo : 1;
            bool presentation : 1;
        } bits;
    } updated;

    bool rejected = false;

    // initial values, copied from protocol text
    CHLBufferReference  buffer = {};                                  // The initial surface contents are void
    CRegion             damage, bufferDamage;                         // The initial value for pending damage is empty
    CRegion             opaque;                                       // The initial value for an opaque region is empty
    CRegion             input;                                        // The initial value for an input region is infinite
    bool                inputIsInfinite = true;                       // Tracks the input region's infinite protocol state
    wl_output_transform transform       = WL_OUTPUT_TRANSFORM_NORMAL; // A newly created surface has its buffer transformation set to normal
    int                 scale           = 1;                          // A newly created surface has its buffer scale set to 1

    // these don't have well defined initial values in the protocol, but these work
    Vector2D size, bufferSize;
    Vector2D offset;

    // for xdg_shell resizing
    Vector2D ackedSize;

    // for wl_surface::frame callbacks.
    std::vector<SP<CWLCallbackResource>> callbacks;

    // for wp_presentation feedbacks, tied to this commit.
    std::vector<WP<CPresentationFeedback>> presentationFeedbacks;

    // viewporter protocol surface state
    struct {
        bool     hasDestination = false;
        bool     hasSource      = false;
        Vector2D destination;
        CBox     source;
    } viewport;
    Vector2D sourceSize();

    // drm syncobj protocol surface state
    CDRMSyncPointState acquire;

    // texture of surface content, used for rendering
    SP<Render::ITexture> texture;
    void                 updateSynchronousTexture(SP<Render::ITexture> lastTexture);

    // fifo
    bool     barrierSet       = false;
    bool     waitBarrier      = false;
    uint64_t fifoBarrierEpoch = 0;
    uint64_t fifoWaitEpoch    = 0;

    // commit timing
    std::optional<Time::steady_tp> commitTimingTarget;
    SP<CEventLoopTimer>            timer;

    // helpers
    CRegion accumulateBufferDamage();                              // transforms state.damage and merges it into state.bufferDamage
    CRegion effectiveInputRegion() const;                          // materializes the input region clipped to the current surface size
    void    updateFrom(SSurfaceState& ref, bool accumulateDamage); // updates this state based on a reference state.
    void    reset();                                               // resets pending state after commit
};
