// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Service.h"

#include "Log.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <thread>

#include <pthread.h>
#include <unistd.h>
#if defined(__FreeBSD__)
#include <sys/procctl.h>
#elif defined(__linux__)
#include <sys/prctl.h>
#endif

namespace Bots {

bool hideFromDebuggers()
{
#if defined(__FreeBSD__)
    int disable = PROC_TRACE_CTL_DISABLE;
    return ::procctl(P_PID, ::getpid(), PROC_TRACE_CTL, &disable) == 0;
#elif defined(__linux__)
    return ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == 0;
#else
    return true;
#endif
}

int runUntilSignalled(const char* name, const std::function<int()>& run, const std::function<void()>& stop)
{
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    std::atomic<bool> ended{false};
    std::thread waiter([&] {
        int signal = 0;
        sigwait(&signals, &signal);
        if (!ended) {
            Log::info(name, "stopping ({})", strsignal(signal));
            stop();
        }
    });
    const int status = run();
    // The waiter waits for a signal still when the bot ended on its own.
    ended = true;
    pthread_kill(waiter.native_handle(), SIGTERM);
    waiter.join();
    return status;
}

} // namespace Bots
