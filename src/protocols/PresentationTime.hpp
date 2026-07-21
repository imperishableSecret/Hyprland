#pragma once

#include <ctime>
#include <vector>
#include <cstdint>
#include "WaylandProtocol.hpp"
#include "presentation-time.hpp"
#include "../helpers/time/Time.hpp"

class CWLSurfaceResource;
class CPresentationFeedback;

class CQueuedPresentationData {
  public:
    CQueuedPresentationData(SP<CWLSurfaceResource> surf, std::vector<WP<CPresentationFeedback>> feedbacks);

    void setPresentationType(bool zeroCopy);
    void attachMonitor(PHLMONITOR pMonitor);

    void presented();
    void discarded();

  private:
    bool                                   m_wasPresented = false;
    bool                                   m_zeroCopy     = false;
    PHLMONITORREF                          m_monitor;
    WP<CWLSurfaceResource>                 m_surface;
    std::vector<WP<CPresentationFeedback>> m_feedbacks;

    friend class CPresentationFeedback;
    friend class CPresentationProtocol;
};

class CPresentationFeedback {
  public:
    CPresentationFeedback(UP<CWpPresentationFeedback>&& resource_, SP<CWLSurfaceResource> surf);

    bool good();

    void sendQueued(WP<CQueuedPresentationData> data, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags);
    void sendDiscarded();

  private:
    UP<CWpPresentationFeedback> m_resource;
    WP<CWLSurfaceResource>      m_surface;
    bool                        m_done = false;

    friend class CPresentationProtocol;
};

class CPresentationProtocol : public IWaylandProtocol {
  public:
    CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

    void         beginOutputFrame(PHLMONITOR pMonitor);
    void         queueData(UP<CQueuedPresentationData>&& data);
    bool         hasStagedData(PHLMONITOR pMonitor) const;
    uint64_t     beginOutputCommit(PHLMONITOR pMonitor, bool bufferCommitted, bool zeroCopy);
    void         finishOutputCommit(PHLMONITOR pMonitor, uint64_t id, bool success);
    void         onPresented(PHLMONITOR pMonitor, uint64_t id, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags);
    void         onDiscarded(PHLMONITOR pMonitor, uint64_t id);
    void         discardFeedbacks(std::vector<WP<CPresentationFeedback>>& feedbacks);
    void         discardFeedbacksForSurface(WP<CWLSurfaceResource> surface);
    bool         hasPendingFeedbacks() const;

  private:
    void onManagerResourceDestroy(wl_resource* res);
    void destroyResource(CPresentationFeedback* feedback);
    void onGetFeedback(CWpPresentation* pMgr, wl_resource* surf, uint32_t id);

    enum eSubmissionState : uint8_t {
        SUBMISSION_STAGED,
        SUBMISSION_COMMITTING,
        SUBMISSION_ACCEPTED,
    };

    struct SSubmission {
        uint64_t                                 id       = 0;
        eSubmissionState                         state    = SUBMISSION_STAGED;
        bool                                     zeroCopy = false;
        std::vector<UP<CQueuedPresentationData>> data;
    };

    struct SOutputPresentationState {
        PHLMONITORREF            monitor;
        uint64_t                 nextID = 1;
        std::vector<SSubmission> submissions;
    };

    SOutputPresentationState* outputStateFor(PHLMONITOR pMonitor, bool create = false);
    static bool               hasStagedData(const SOutputPresentationState& outputState);
    static uint64_t           beginOutputCommit(SOutputPresentationState& outputState, bool bufferCommitted, bool zeroCopy);
    static void               finishOutputCommit(SOutputPresentationState& outputState, uint64_t id, bool success);
    static SSubmission*       submissionFor(SOutputPresentationState& outputState, uint64_t id);
    void                      discardSubmission(SSubmission& submission);
    void                      removeDoneFeedbacks();

    //
    std::vector<UP<CWpPresentation>>       m_managers;
    std::vector<UP<CPresentationFeedback>> m_feedbacks;
    std::vector<SOutputPresentationState>  m_outputStates;

    friend class CPresentationFeedback;
    friend class CPresentationProtocolTestAccess;
};

namespace PROTO {
    inline UP<CPresentationProtocol> presentation;
};
