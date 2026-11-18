// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GatewaySession.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace Bots {

class Discord;

/// The gateway's connection, on a thread of its own so that a slow REST call
/// of the bot never delays a heartbeat. It connects through libcurl's
/// WebSocket support, connects again (resuming when it can) whenever the
/// session says so, and hands each dispatch to @p onEvent, on its thread.
class Gateway
{
public:
    using EventHandler = std::function<void(GatewayEvent)>;
    /// Called once when Discord refuses the bot for good.
    using FatalHandler = std::function<void(const std::string&)>;

    /// @p name: the bot's; @p userAgent: as Discord's.
    Gateway(std::string token, std::string name, std::string userAgent, EventHandler onEvent, FatalHandler onFatal);
    ~Gateway();
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    void start();
    void stop();

private:
    void run();
    /// One connection, from open to close. False when it could not open.
    bool connection(const std::string& url);
    std::string gatewayUrl(Discord& rest);

    std::string m_token;
    std::string m_userAgent;
    EventHandler m_onEvent;
    FatalHandler m_onFatal;
    GatewaySession m_session;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

} // namespace Bots
