// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Wisp: phosphor-works' Discord bot. Its settings come from the environment
// (the service exports its env file): docs/WISP.md.

#include "Config.h"
#include "Http.h"
#include "Log.h"
#include "Service.h"
#include "Version.h"
#include "Wisp.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv)
{
    using namespace Wisp;
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
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
    Bot bot(std::move(config), makeCurlTransport(), makeCurlTransport());
    return runUntilSignalled(
        "wisp",
        [&] {
            return bot.run();
        },
        [&] {
            bot.stop();
        });
}
