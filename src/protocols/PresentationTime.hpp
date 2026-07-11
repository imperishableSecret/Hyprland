#pragma once

#include "../helpers/signal/Signal.hpp"
#include "../output/FrameSubmission.hpp"
#include "WaylandProtocol.hpp"
#include "presentation-time.hpp"

#include <cstdint>
#include <vector>

class CWLSurfaceResource;

class CPresentationFeedback {
  public:
    CPresentationFeedback(UP<CWpPresentationFeedback>&& resource, SP<CWLSurfaceResource> surface);

    bool good();
    void sendPresented(PHLMONITOR monitor, const Monitor::SFramePresentation& event);
    void discard();

  private:
    UP<CWpPresentationFeedback> m_resource;
    WP<CWLSurfaceResource>      m_surface;
    WP<CPresentationFeedback>   m_self;
    CHyprSignalListener         m_surfaceDestroy;
    bool                        m_done = false;

    friend class CPresentationFeedbackBatch;
    friend class CPresentationProtocol;
};

class CPresentationFeedbackBatch : public Monitor::IFrameSubmissionWork {
  public:
    ~CPresentationFeedbackBatch() override;

    void         add(WP<CPresentationFeedback> feedback);
    void         attachMonitor(PHLMONITOR monitor);
    bool         empty() const;

    virtual void submitted() override;
    virtual void presented(const Monitor::SFramePresentation& event) override;
    virtual void discarded() override;

  private:
    std::vector<WP<CPresentationFeedback>> m_feedbacks;
    PHLMONITORREF                          m_monitor;
    bool                                   m_done = false;
};

class CPresentationProtocol : public IWaylandProtocol {
  public:
    CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name);

    virtual void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

  private:
    void                                   onManagerResourceDestroy(wl_resource* resource);
    void                                   destroyResource(CPresentationFeedback* feedback);
    void                                   onGetFeedback(CWpPresentation* manager, wl_resource* surface, uint32_t id);
    void                                   feedbackDone(CPresentationFeedback* feedback);
    void                                   pruneDoneFeedbacks();
    void                                   scheduleFeedbackPrune();

    std::vector<UP<CWpPresentation>>       m_managers;
    std::vector<SP<CPresentationFeedback>> m_feedbacks;
    bool                                   m_feedbackPruneScheduled = false;

    friend class CPresentationFeedback;
};

namespace PROTO {
    inline UP<CPresentationProtocol> presentation;
};
