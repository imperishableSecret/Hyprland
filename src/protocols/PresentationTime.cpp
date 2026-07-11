#include "PresentationTime.hpp"

#include "../managers/eventLoop/EventLoopManager.hpp"
#include "../output/Monitor.hpp"
#include "core/Compositor.hpp"
#include "core/Output.hpp"

#include <aquamarine/output/Output.hpp>

#include <algorithm>

static constexpr size_t MAX_PRESENTATION_FEEDBACKS      = 10000;
static constexpr size_t RETAINED_PRESENTATION_FEEDBACKS = 9000;

CPresentationFeedback::CPresentationFeedback(UP<CWpPresentationFeedback>&& resource, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource)), m_surface(surface) {
    if UNLIKELY (!good())
        return;

    if (surface)
        m_surfaceDestroy = surface->m_events.destroy.listen([this] { discard(); });

    m_resource->setOnDestroy([this](CWpPresentationFeedback*) {
        if (!m_done)
            PROTO::presentation->destroyResource(this);
    });
}

bool CPresentationFeedback::good() {
    return m_resource->resource();
}

void CPresentationFeedback::sendPresented(PHLMONITOR monitor, const Monitor::SFramePresentation& event) {
    if (m_done || !good())
        return;

    if (!monitor || !event.presented) {
        discard();
        return;
    }

    const auto CLIENT = m_resource->client();
    if (PROTO::outputs.contains(monitor->m_name)) {
        for (const auto& resource : PROTO::outputs.at(monitor->m_name)->outputResourcesFrom(CLIENT))
            m_resource->sendSyncOutput(resource->getResource()->resource());
    }

    uint32_t flags = 0;
    if (event.flags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_VSYNC)
        flags |= WP_PRESENTATION_FEEDBACK_KIND_VSYNC;
    if (event.flags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_CLOCK)
        flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;
    if (event.flags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_COMPLETION)
        flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;
    if (event.zeroCopy)
        flags |= WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;

    time_t highSeconds = 0;
    if (sizeof(time_t) > 4)
        highSeconds = event.when.tv_sec >> 32;

    const uint32_t REFRESH = m_resource->version() == 1 && monitor->m_vrrActive && monitor->m_output->vrrCapable ? 0 : event.refresh;

    m_done = true;
    if (PROTO::presentation)
        PROTO::presentation->feedbackDone(this);
    m_resource->sendPresented(sc<uint32_t>(highSeconds), sc<uint32_t>(event.when.tv_sec & 0xFFFFFFFF), sc<uint32_t>(event.when.tv_nsec), REFRESH,
                              sc<uint32_t>(event.sequence >> 32), sc<uint32_t>(event.sequence & 0xFFFFFFFF), sc<wpPresentationFeedbackKind>(flags));
}

void CPresentationFeedback::discard() {
    if (m_done || !good())
        return;

    m_done = true;
    if (PROTO::presentation)
        PROTO::presentation->feedbackDone(this);
    m_resource->sendDiscarded();
}

CPresentationFeedbackBatch::~CPresentationFeedbackBatch() {
    discarded();
}

void CPresentationFeedbackBatch::add(WP<CPresentationFeedback> feedback) {
    if (feedback)
        m_feedbacks.emplace_back(std::move(feedback));
}

void CPresentationFeedbackBatch::attachMonitor(PHLMONITOR monitor) {
    if (!m_monitor)
        m_monitor = monitor;
}

bool CPresentationFeedbackBatch::empty() const {
    return std::ranges::none_of(m_feedbacks, [](const auto& feedback) { return !feedback.expired(); });
}

void CPresentationFeedbackBatch::submitted() {
    ;
}

void CPresentationFeedbackBatch::presented(const Monitor::SFramePresentation& event) {
    if (m_done)
        return;

    m_done             = true;
    const auto MONITOR = m_monitor.lock();
    for (const auto& weakFeedback : m_feedbacks) {
        if (const auto FEEDBACK = weakFeedback.lock())
            FEEDBACK->sendPresented(MONITOR, event);
    }
    m_feedbacks.clear();
}

void CPresentationFeedbackBatch::discarded() {
    if (m_done)
        return;

    m_done = true;
    for (const auto& weakFeedback : m_feedbacks) {
        if (const auto FEEDBACK = weakFeedback.lock())
            FEEDBACK->discard();
    }
    m_feedbacks.clear();
}

CPresentationProtocol::CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    ;
}

void CPresentationProtocol::bindManager(wl_client* client, void*, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CWpPresentation>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CWpPresentation* manager) { onManagerResourceDestroy(manager->resource()); });
    RESOURCE->setDestroy([this](CWpPresentation* manager) { onManagerResourceDestroy(manager->resource()); });
    RESOURCE->setFeedback([this](CWpPresentation* manager, wl_resource* surface, uint32_t feedbackID) { onGetFeedback(manager, surface, feedbackID); });
    RESOURCE->sendClockId(CLOCK_MONOTONIC);
}

void CPresentationProtocol::onManagerResourceDestroy(wl_resource* resource) {
    std::erase_if(m_managers, [&](const auto& manager) { return manager->resource() == resource; });
}

void CPresentationProtocol::destroyResource(CPresentationFeedback* feedback) {
    std::erase_if(m_feedbacks, [&](const auto& other) { return other.get() == feedback; });
}

void CPresentationProtocol::onGetFeedback(CWpPresentation* manager, wl_resource* surfaceResource, uint32_t id) {
    pruneDoneFeedbacks();

    if (m_feedbacks.size() >= MAX_PRESENTATION_FEEDBACKS) {
        LOGM(Log::ERR, "Presentation feedback queue reached {} entries, discarding oldest feedback", m_feedbacks.size());
        const size_t DROP_COUNT = m_feedbacks.size() - RETAINED_PRESENTATION_FEEDBACKS + 1;
        for (size_t i = 0; i < DROP_COUNT; ++i)
            m_feedbacks[i]->discard();
        pruneDoneFeedbacks();
    }

    const auto SURFACE  = CWLSurfaceResource::fromResource(surfaceResource);
    const auto FEEDBACK = makeShared<CPresentationFeedback>(makeUnique<CWpPresentationFeedback>(manager->client(), manager->version(), id), SURFACE);
    FEEDBACK->m_self    = FEEDBACK;
    m_feedbacks.emplace_back(FEEDBACK);

    if UNLIKELY (!FEEDBACK->good()) {
        manager->noMemory();
        m_feedbacks.pop_back();
        return;
    }

    if (!SURFACE) {
        FEEDBACK->discard();
        return;
    }

    if (!SURFACE->m_pending.presentationFeedback)
        SURFACE->m_pending.presentationFeedback = makeShared<CPresentationFeedbackBatch>();
    SURFACE->m_pending.presentationFeedback->add(FEEDBACK);
}

void CPresentationProtocol::feedbackDone(CPresentationFeedback*) {
    scheduleFeedbackPrune();
}

void CPresentationProtocol::pruneDoneFeedbacks() {
    for (const auto& feedback : m_feedbacks) {
        if (feedback && !feedback->m_done && !feedback->m_surface)
            feedback->discard();
    }
    std::erase_if(m_feedbacks, [](const auto& feedback) { return !feedback || feedback->m_done || !feedback->m_surface; });
}

void CPresentationProtocol::scheduleFeedbackPrune() {
    if (m_feedbackPruneScheduled || !g_pEventLoopManager)
        return;

    m_feedbackPruneScheduled = true;
    g_pEventLoopManager->doLater([this] {
        m_feedbackPruneScheduled = false;
        pruneDoneFeedbacks();
    });
}
