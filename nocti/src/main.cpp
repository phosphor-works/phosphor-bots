// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

// Nocti: a Claude Code session on Discord as a bot of its own. Claude Code
// starts it as an MCP server on stdio (.mcp.json); its settings come from
// ~/.config/nocti/nocti.env, or the file --env names: docs/NOCTI.md.

#include "Config.h"
#include "Http.h"
#include "Log.h"
#include "McpServer.h"
#include "Nocti.h"
#include "Service.h"
#include "Version.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    using namespace Nocti;
    std::string envPath;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--version") {
            std::printf("nocti %s\n", Version);
            return 0;
        }
        if (argument == "--env" && i + 1 < argc)
            envPath = argv[++i];
    }
    if (!hideFromDebuggers()) {
        Log::info("nocti", "could not keep debuggers out: {}", std::strerror(errno));
        return 1;
    }
    Config config;
    try {
        config = loadConfig(envPath.empty() ? defaultEnvPath() : envPath);
    } catch (const std::exception& error) {
        Log::info("nocti", "{}", error.what());
        return 2;
    }
    initHttp();
    std::ios::sync_with_stdio(false);

    McpServer mcp({"nocti", Version, Bot::instructions(config), true, {"claude-code"}, config.relay}, std::cin,
                  std::cout);
    Bot bot(std::move(config), makeCurlTransport(), mcp);
    bot.registerTools();
    // Discord hears nothing until the session is initialized.
    mcp.onInitialized = [&bot] {
        bot.start();
    };
    Log::info("nocti", "{}", bot.passive() ? "up, but another session holds Nocti" : "up; waiting for the session");
    mcp.run();
    bot.stop();
    return 0;
}
