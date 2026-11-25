// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

// Glim: files posts of phosphor-works' Discord help forum as GitHub issues,
// when someone mentions it. Its settings come from the environment (the
// service exports its env file): docs/GLIM.md.

#include "Config.h"
#include "Files.h"
#include "GitHubApp.h"
#include "Glim.h"
#include "Http.h"
#include "Log.h"
#include "Service.h"
#include "Version.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <format>
#include <memory>

int main(int argc, char** argv)
{
    using namespace Glim;
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("glim %s\n", Version);
        return 0;
    }
    if (!hideFromDebuggers()) {
        Log::info("glim", "could not keep debuggers out: {}", std::strerror(errno));
        return 1;
    }
    Config config;
    std::unique_ptr<GitHubApp> app;
    try {
        config = parseConfig(processEnvironment());
        std::string error;
        const auto key = readFile(config.appKeyPath, &error);
        if (!key)
            throw std::runtime_error(
                std::format("GLIM_APP_KEY: {}: {}", config.appKeyPath, error.empty() ? "no such file" : error));
        initHttp();
        app = std::make_unique<GitHubApp>(config.appId, *key, config.org,
                                          std::format("glim/{} (+{})", Version, HomePage), makeCurlTransport());
    } catch (const std::exception& error) {
        Log::info("glim", "{}", error.what());
        return 2;
    }
    Bot bot(std::move(config), makeCurlTransport(), makeCurlTransport(), [&app] {
        return app->token();
    });
    return runUntilSignalled(
        "glim",
        [&] {
            return bot.run();
        },
        [&] {
            bot.stop();
        });
}
