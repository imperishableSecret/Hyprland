#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <wayland-server.h>
#include "../../helpers/signal/Signal.hpp"
#include <hyprutils/os/FileDescriptor.hpp>

#include "EventLoopTimer.hpp"

namespace Aquamarine {
    struct SPollFD;
};

struct SEventLoopDoLaterLock {
    SEventLoopDoLaterLock(uint64_t seq);
    ~SEventLoopDoLaterLock();

    uint64_t seq = 0;
};

struct SEventLoopReadableWaiter {
    wl_event_source*               source = nullptr;
    Hyprutils::OS::CFileDescriptor fd;
    std::function<void()>          fn;
    bool                           failed = false;

    SEventLoopReadableWaiter(Hyprutils::OS::CFileDescriptor fd_, std::function<void()> fn_);
    ~SEventLoopReadableWaiter();

    SEventLoopReadableWaiter(const SEventLoopReadableWaiter&)            = delete;
    SEventLoopReadableWaiter& operator=(const SEventLoopReadableWaiter&) = delete;
};

class CEventLoopManager {
  public:
    CEventLoopManager(wl_display* display, wl_event_loop* wlEventLoop);
    ~CEventLoopManager();

    void enterLoop();

    // Note: will remove the timer if the ptr is lost.
    void addTimer(SP<CEventLoopTimer> timer);
    void removeTimer(SP<CEventLoopTimer> timer);

    void onTimerFire();

    // schedules a recalc of the timers
    void scheduleRecalc();

    // schedules a function to run later, aka in a wayland idle event. Returns a sequence which can be used to remove it.
    uint64_t doLater(const std::function<void()>& fn);
    void     removeDoLater(uint64_t seq);

    // automatically cleaned up doLater instance
    [[nodiscard]] UP<SEventLoopDoLaterLock> doLaterLock(const std::function<void()>& fn);

    struct SIdleData {
        wl_event_source*                                        eventSource = nullptr;
        std::vector<std::pair<uint64_t, std::function<void()>>> fns;
    };

    // schedule function to when fd is readable (WL_EVENT_READABLE / POLLIN),
    // takes ownership of fd
    WP<SEventLoopReadableWaiter> doOnReadable(Hyprutils::OS::CFileDescriptor fd, std::function<void()>&& fn);
    void                         removeOnReadable(const WP<SEventLoopReadableWaiter>& waiter);
    void                         onFdReadable(SEventLoopReadableWaiter* waiter);
    void                         onFdReadableFail(SEventLoopReadableWaiter* waiter);

  private:
    // Manages the event sources after AQ pollFDs change.
    void syncPollFDs();
    void nudgeTimers();

    struct SEventSourceData {
        SP<Aquamarine::SPollFD> pollFD;
        wl_event_source*        eventSource = nullptr;
    };

    struct {
        wl_event_loop*   loop        = nullptr;
        wl_display*      display     = nullptr;
        wl_event_source* eventSource = nullptr;
    } m_wayland;

    struct {
        std::vector<SP<CEventLoopTimer>> timers;
        Hyprutils::OS::CFileDescriptor   timerfd;
        bool                             recalcScheduled = false;
    } m_timers;

    SIdleData                                 m_idle;
    std::map<int, SEventSourceData>           m_aqEventSources;
    std::vector<SP<SEventLoopReadableWaiter>> m_readableWaiters;

    struct {
        CHyprSignalListener pollFDsChanged;
    } m_listeners;

    wl_event_source* m_configWatcherInotifySource = nullptr;

    friend class CAsyncDialogBox;
    friend class CMainLoopExecutor;
};

inline UP<CEventLoopManager> g_pEventLoopManager;
