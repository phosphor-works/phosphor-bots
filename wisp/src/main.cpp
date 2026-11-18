// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Wisp: phosphor-works' Discord bot. Its settings come from the environment
// (the service exports its env file): docs/WISP.md.

#include "Config.h"
#include "Http.h"
#include "Log.h"
#include "Version.h"
#include "Wisp.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <exception>
#include <thread>

#include <pthread.h>
#include <unistd.h>
#if defined(__FreeBSD__)
#include <sys/procctl.h>
#elif defined(__linux__)
#include <sys/prctl.h>
#endif

namespace {

/// Keeps the process's memory and environment, where the tokens are, from
/// debuggers of its own user.
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

} // namespace

int main(int argc, char** argv)
{
    using namespace Wisp;
    if (argc > 1 && (std::strcmp(argv[1], "--version") == 0)) {
        std::printf("wisp %s\n", Version);
        return 0;
    }
    if (!hideFromDebuggers()) {
        Log::info("wisp", "could not keep debuggers out: {}", std::strerror(errno));
        return 1;
    }

    Config config;
    try {
        config = parseConfig(processEnvironment());
    } catch (const std::exception& error) {
        Log::info("wisp", "{}", error.what());
        return 2;
    }
    initHttp();

    // SIGTERM and SIGINT reach one thread, which stops the bot.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    Bot bot(std::move(config), makeCurlTransport(), makeCurlTransport());
    std::atomic<bool> ended{false};
    std::thread waiter([&] {
        int signal = 0;
        sigwait(&signals, &signal);
        if (!ended) {
            Log::info("wisp", "stopping ({})", strsignal(signal));
            bot.stop();
        }
    });
    const int status = bot.run();
    // The waiter waits for a signal still when the bot ended on its own.
    ended = true;
    pthread_kill(waiter.native_handle(), SIGTERM);
    waiter.join();
    return status;
}
