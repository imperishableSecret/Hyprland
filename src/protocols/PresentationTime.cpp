#include "PresentationTime.hpp"
#include <algorithm>
#include "../output/Monitor.hpp"
#include "../event/EventBus.hpp"
#include "core/Compositor.hpp"
#include "core/Output.hpp"
#include <aquamarine/output/Output.hpp>

CQueuedPresentationData::CQueuedPresentationData(SP<CWLSurfaceResource> surf, std::vector<WP<CPresentationFeedback>> feedbacks) :
    m_surface(surf), m_feedbacks(std::move(feedbacks)) {
    ;
}

void CQueuedPresentationData::setPresentationType(bool zeroCopy_) {
    m_zeroCopy = zeroCopy_;
}

void CQueuedPresentationData::attachMonitor(PHLMONITOR pMonitor_) {
    m_monitor = pMonitor_;
}

void CQueuedPresentationData::presented() {
    m_wasPresented = true;
}

void CQueuedPresentationData::discarded() {
    m_wasPresented = false;
}

CPresentationFeedback::CPresentationFeedback(UP<CWpPresentationFeedback>&& resource_, SP<CWLSurfaceResource> surf) : m_resource(std::move(resource_)), m_surface(surf) {
    if UNLIKELY (!good())
        return;

    m_resource->setOnDestroy([this](CWpPresentationFeedback* pMgr) {
        if (!m_done) // if it's done, it's probably already destroyed. If not, it will be in a sec.
            PROTO::presentation->destroyResource(this);
    });
}

bool CPresentationFeedback::good() {
    return m_resource->resource();
}

void CPresentationFeedback::sendQueued(WP<CQueuedPresentationData> data, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags) {
    auto client = m_resource->client();

    if LIKELY (PROTO::outputs.contains(data->m_monitor->m_name) && data->m_wasPresented) {
        if LIKELY (auto outputResources = PROTO::outputs.at(data->m_monitor->m_name)->outputResourcesFrom(client); !outputResources.empty()) {
            for (const auto& r : outputResources) {
                m_resource->sendSyncOutput(r->getResource()->resource());
            }
        }
    }

    if (data->m_wasPresented) {
        uint32_t flags = 0;
        if (!data->m_monitor->m_tearingState.activelyTearing)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_VSYNC;
        if (data->m_zeroCopy)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;
        if (reportedFlags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_CLOCK)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;
        if (reportedFlags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_HW_COMPLETION)
            flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;

        time_t tv_sec = 0;
        if (sizeof(time_t) > 4)
            tv_sec = when.tv_sec >> 32;

        uint32_t refreshNs = m_resource->version() == 1 && data->m_monitor->m_vrrActive && data->m_monitor->m_output->vrrCapable ? 0 : untilRefreshNs;

        m_resource->sendPresented(sc<uint32_t>(tv_sec), sc<uint32_t>(when.tv_sec & 0xFFFFFFFF), sc<uint32_t>(when.tv_nsec), refreshNs, sc<uint32_t>(seq >> 32),
                                  sc<uint32_t>(seq & 0xFFFFFFFF), sc<wpPresentationFeedbackKind>(flags));
    } else
        m_resource->sendDiscarded();

    m_done = true;
}

void CPresentationFeedback::sendDiscarded() {
    if (m_done)
        return;

    m_resource->sendDiscarded();
    m_done = true;
}

CPresentationProtocol::CPresentationProtocol(const wl_interface* iface, const int& ver, const std::string& name) : IWaylandProtocol(iface, ver, name) {
    static auto P = Event::bus()->m_events.monitor.removed.listen([this](PHLMONITOR mon) {
        const auto STATE = outputStateFor(mon);
        if (!STATE)
            return;

        for (auto& submission : STATE->submissions)
            discardSubmission(submission);

        std::erase_if(m_outputStates, [mon](const auto& state) { return state.monitor == mon; });
    });
}

void CPresentationProtocol::bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) {
    const auto RESOURCE = m_managers.emplace_back(makeUnique<CWpPresentation>(client, ver, id)).get();
    RESOURCE->setOnDestroy([this](CWpPresentation* p) { this->onManagerResourceDestroy(p->resource()); });

    RESOURCE->setDestroy([this](CWpPresentation* pMgr) { this->onManagerResourceDestroy(pMgr->resource()); });
    RESOURCE->setFeedback([this](CWpPresentation* pMgr, wl_resource* surf, uint32_t id) { this->onGetFeedback(pMgr, surf, id); });
    RESOURCE->sendClockId(CLOCK_MONOTONIC);
}

void CPresentationProtocol::onManagerResourceDestroy(wl_resource* res) {
    std::erase_if(m_managers, [&](const auto& other) { return other->resource() == res; });
}

void CPresentationProtocol::destroyResource(CPresentationFeedback* feedback) {
    feedback->m_done = true;
    std::erase_if(m_feedbacks, [&](const auto& other) { return other.get() == feedback; });
}

void CPresentationProtocol::onGetFeedback(CWpPresentation* pMgr, wl_resource* surf, uint32_t id) {
    const auto  CLIENT = pMgr->client();
    const auto& RESOURCE =
        m_feedbacks.emplace_back(makeUnique<CPresentationFeedback>(makeUnique<CWpPresentationFeedback>(CLIENT, pMgr->version(), id), CWLSurfaceResource::fromResource(surf)));

    if UNLIKELY (!RESOURCE->good()) {
        pMgr->noMemory();
        m_feedbacks.pop_back();
        return;
    }

    if (const auto SURFACE = CWLSurfaceResource::fromResource(surf); SURFACE) {
        SURFACE->m_pending.presentationFeedbacks.emplace_back(RESOURCE);
        SURFACE->m_pending.updated.bits.presentation = true;
    }
}

void CPresentationProtocol::removeDoneFeedbacks() {
    if (m_feedbacks.size() > 10000) {
        LOGM(Log::ERR, "FIXME: presentation has a feedback leak, and has grown to {} pending entries!!! Dropping!!!!!", m_feedbacks.size());

        // Move the elements from the 9000th position to the end of the vector.
        std::vector<UP<CPresentationFeedback>> newFeedbacks;
        newFeedbacks.reserve(m_feedbacks.size() - 9000);

        for (auto it = m_feedbacks.begin() + 9000; it != m_feedbacks.end(); ++it) {
            newFeedbacks.push_back(std::move(*it));
        }

        m_feedbacks = std::move(newFeedbacks);
    }

    std::erase_if(m_feedbacks, [](const auto& other) { return !other->m_surface || other->m_done; });
}

CPresentationProtocol::SOutputPresentationState* CPresentationProtocol::outputStateFor(PHLMONITOR pMonitor, bool create) {
    if (!pMonitor)
        return nullptr;

    const auto STATE = std::ranges::find_if(m_outputStates, [pMonitor](const auto& state) { return state.monitor == pMonitor; });
    if (STATE != m_outputStates.end())
        return &*STATE;

    if (!create)
        return nullptr;

    return &m_outputStates.emplace_back(SOutputPresentationState{.monitor = pMonitor});
}

void CPresentationProtocol::discardSubmission(SSubmission& submission) {
    for (auto& data : submission.data) {
        if (data)
            discardFeedbacks(data->m_feedbacks);
    }

    submission.data.clear();
}

void CPresentationProtocol::beginOutputFrame(PHLMONITOR pMonitor) {
    const auto STATE = outputStateFor(pMonitor);
    if (!STATE)
        return;

    for (auto& submission : STATE->submissions) {
        if (submission.state == SUBMISSION_STAGED)
            discardSubmission(submission);
    }

    std::erase_if(STATE->submissions, [](const auto& submission) { return submission.state == SUBMISSION_STAGED; });
}

void CPresentationProtocol::queueData(UP<CQueuedPresentationData>&& data) {
    if (!data || !data->m_monitor) {
        if (data)
            discardFeedbacks(data->m_feedbacks);
        return;
    }

    const auto STATE      = outputStateFor(data->m_monitor.lock(), true);
    const auto SUBMISSION = std::ranges::find_if(STATE->submissions, [](const auto& submission) { return submission.state == SUBMISSION_STAGED; });
    if (SUBMISSION != STATE->submissions.end()) {
        SUBMISSION->data.emplace_back(std::move(data));
        return;
    }

    auto& submission = STATE->submissions.emplace_back();
    submission.data.emplace_back(std::move(data));
}

bool CPresentationProtocol::hasStagedData(PHLMONITOR pMonitor) const {
    const auto STATE = std::ranges::find_if(m_outputStates, [pMonitor](const auto& state) { return state.monitor == pMonitor; });
    return STATE != m_outputStates.end() && hasStagedData(*STATE);
}

bool CPresentationProtocol::hasStagedData(const SOutputPresentationState& outputState) {
    return std::ranges::any_of(outputState.submissions, [](const auto& submission) { return submission.state == SUBMISSION_STAGED && !submission.data.empty(); });
}

uint64_t CPresentationProtocol::beginOutputCommit(PHLMONITOR pMonitor, bool bufferCommitted, bool zeroCopy) {
    const auto STATE = outputStateFor(pMonitor);
    return STATE ? beginOutputCommit(*STATE, bufferCommitted, zeroCopy) : 0;
}

uint64_t CPresentationProtocol::beginOutputCommit(SOutputPresentationState& outputState, bool bufferCommitted, bool zeroCopy) {
    if (!bufferCommitted)
        return 0;

    const auto SUBMISSION = std::ranges::find_if(outputState.submissions, [](const auto& submission) { return submission.state == SUBMISSION_STAGED && !submission.data.empty(); });
    if (SUBMISSION == outputState.submissions.end())
        return 0;

    if (outputState.nextID == 0)
        outputState.nextID = 1;

    SUBMISSION->id       = outputState.nextID++;
    SUBMISSION->state    = SUBMISSION_COMMITTING;
    SUBMISSION->zeroCopy = zeroCopy;

    if (outputState.nextID == 0)
        outputState.nextID = 1;

    return SUBMISSION->id;
}

void CPresentationProtocol::finishOutputCommit(PHLMONITOR pMonitor, uint64_t id, bool success) {
    if (id == 0)
        return;

    const auto STATE = outputStateFor(pMonitor);
    if (!STATE)
        return;

    finishOutputCommit(*STATE, id, success);
}

void CPresentationProtocol::finishOutputCommit(SOutputPresentationState& outputState, uint64_t id, bool success) {
    const auto SUBMISSION = submissionFor(outputState, id);
    if (!SUBMISSION)
        return;

    if (success) {
        SUBMISSION->state = SUBMISSION_ACCEPTED;
        return;
    }

    SUBMISSION->id       = 0;
    SUBMISSION->state    = SUBMISSION_STAGED;
    SUBMISSION->zeroCopy = false;
}

CPresentationProtocol::SSubmission* CPresentationProtocol::submissionFor(SOutputPresentationState& outputState, uint64_t id) {
    const auto SUBMISSION = std::ranges::find_if(outputState.submissions, [id](const auto& submission) { return submission.id == id; });
    return SUBMISSION == outputState.submissions.end() ? nullptr : &*SUBMISSION;
}

void CPresentationProtocol::onPresented(PHLMONITOR pMonitor, uint64_t id, const timespec& when, uint32_t untilRefreshNs, uint64_t seq, uint32_t reportedFlags) {
    if (id == 0)
        return;

    const auto STATE = outputStateFor(pMonitor);
    if (!STATE)
        return;

    const auto SUBMISSION = submissionFor(*STATE, id);
    if (!SUBMISSION) {
        LOGM(Log::TRACE, "Ignoring presentation event for unknown output submission {}", id);
        return;
    }

    for (auto const& data : SUBMISSION->data) {
        if (!data || !data->m_surface || !data->m_monitor) {
            if (data)
                discardFeedbacks(data->m_feedbacks);
            continue;
        }

        data->setPresentationType(SUBMISSION->zeroCopy && (reportedFlags & Aquamarine::IOutput::AQ_OUTPUT_PRESENT_ZEROCOPY));
        for (auto const& feedback : data->m_feedbacks) {
            if (feedback && !feedback->m_done)
                feedback->sendQueued(data, when, untilRefreshNs, seq, reportedFlags);
        }
    }

    std::erase_if(STATE->submissions, [id](const auto& submission) { return submission.id == id; });
    removeDoneFeedbacks();
}

void CPresentationProtocol::onDiscarded(PHLMONITOR pMonitor, uint64_t id) {
    if (id == 0)
        return;

    const auto STATE = outputStateFor(pMonitor);
    if (!STATE)
        return;

    const auto SUBMISSION = submissionFor(*STATE, id);
    if (!SUBMISSION) {
        LOGM(Log::TRACE, "Ignoring discard event for unknown output submission {}", id);
        return;
    }

    discardSubmission(*SUBMISSION);
    std::erase_if(STATE->submissions, [id](const auto& submission) { return submission.id == id; });
    removeDoneFeedbacks();
}

void CPresentationProtocol::discardFeedbacks(std::vector<WP<CPresentationFeedback>>& feedbacks) {
    for (auto const& feedback : feedbacks) {
        if (!feedback || feedback->m_done)
            continue;

        feedback->sendDiscarded();
    }

    feedbacks.clear();
    removeDoneFeedbacks();
}

void CPresentationProtocol::discardFeedbacksForSurface(WP<CWLSurfaceResource> surface) {
    if (!surface)
        return;

    for (auto const& feedback : m_feedbacks) {
        if (feedback->m_surface != surface)
            continue;

        feedback->sendDiscarded();
    }

    for (auto& state : m_outputStates) {
        for (auto& submission : state.submissions)
            std::erase_if(submission.data, [surface](const auto& data) { return !data || !data->m_surface || data->m_surface == surface; });

        std::erase_if(state.submissions, [](const auto& submission) { return submission.data.empty(); });
    }

    std::erase_if(m_outputStates, [](const auto& state) { return state.submissions.empty(); });
    removeDoneFeedbacks();
}

bool CPresentationProtocol::hasPendingFeedbacks() const {
    return !m_feedbacks.empty();
}
