#pragma once

#include "../helpers/memory/Memory.hpp"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <vector>

namespace Monitor {
    struct SFramePresentation {
        bool     presented = false;
        timespec when      = {};
        uint32_t refresh   = 0;
        uint64_t sequence  = 0;
        uint32_t flags     = 0;
        bool     zeroCopy  = false;
    };

    class IFrameSubmissionWork {
      public:
        virtual ~IFrameSubmissionWork() = default;

        virtual void submitted()                                = 0;
        virtual void presented(const SFramePresentation& event) = 0;
        virtual void discarded()                                = 0;
    };

    class CFrameSubmissionLedger {
      public:
        struct SStats {
            uint64_t staged    = 0;
            uint64_t submitted = 0;
            uint64_t presented = 0;
            uint64_t discarded = 0;
            uint64_t aborted   = 0;
            uint64_t orphaned  = 0;
        };

        uint64_t begin();
        void     attach(SP<IFrameSubmissionWork> work);
        uint64_t beginCommit(bool zeroCopy);
        uint64_t finishCommit(bool success);
        void     abort();
        bool     complete(const SFramePresentation& event);
        void     discardAll();

        bool     hasStagedSubmission() const;
        bool     hasStagedWork() const;
        size_t   stagedWorkCount() const;
        size_t   inFlightCount() const;
        uint64_t stagedID() const;
        uint64_t oldestInFlightID() const;
        uint64_t presentationTargetID() const;
        SStats   stats() const;

      private:
        struct SSubmission {
            uint64_t                              id       = 0;
            bool                                  zeroCopy = false;
            std::vector<SP<IFrameSubmissionWork>> work;
        };

        uint64_t                          nextID();
        void                              submit(SSubmission& submission);
        void                              discard(SSubmission& submission);
        void                              completeSubmission(SSubmission& submission, const SFramePresentation& event);

        uint64_t                          m_nextID = 1;
        std::optional<SSubmission>        m_staged;
        std::optional<SSubmission>        m_committing;
        std::optional<SFramePresentation> m_presentationDuringCommit;
        std::optional<SSubmission>        m_inFlight;
        SStats                            m_stats;
    };
}
