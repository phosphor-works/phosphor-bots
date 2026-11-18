// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Bots {

/// A dispatch of the gateway that the bot handles (MESSAGE_CREATE, ...).
struct GatewayEvent
{
    std::string type;
    nlohmann::json data;
};

/// Discord's gateway protocol, v10, without the socket: what to send for what
/// came, and when the connection must go. Gateway gives it the socket.
///
/// A connection that has not become ready (READY, RESUMED) within three
/// heartbeat intervals is given up, as is one whose heartbeat Discord did not
/// acknowledge before the next: a half-open connection otherwise waits for
/// good.
class GatewaySession
{
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Random = std::function<double()>; ///< in [0, 1)

    /// Intents: GUILDS, GUILD_MESSAGES and MESSAGE_CONTENT (privileged: the
    /// application's Message Content Intent must be on).
    static constexpr int Intents = (1 << 0) | (1 << 9) | (1 << 15);

    /// @p name: the bot's, in IDENTIFY's properties.
    GatewaySession(std::string token, std::string name, Random random = {});

    /// A socket to @p url is open (resumeUrl() when resuming).
    void connected(TimePoint now);
    /// One text message from Discord. Returns the payloads to send.
    std::vector<std::string> receive(const std::string& text, TimePoint now);
    /// Time passed. Returns the payloads to send (a heartbeat when due).
    std::vector<std::string> tick(TimePoint now);
    /// Discord closed the connection with @p code (0: no close frame).
    void closed(int code);

    /// When tick() next has something to do.
    TimePoint nextDeadline() const;
    /// The connection must be closed now (and a new one opened, unless fatal()).
    bool mustReconnect() const
    {
        return m_reconnect;
    }
    /// Discord refused the bot for good (a bad token, intents not allowed).
    bool fatal() const
    {
        return !m_fatal.empty();
    }
    const std::string& fatalReason() const
    {
        return m_fatal;
    }
    /// Why the last connection is given up, for the log.
    const std::string& reconnectReason() const
    {
        return m_reason;
    }
    /// How long to wait before the next connection (an invalid session's).
    std::chrono::milliseconds reconnectDelay() const
    {
        return m_delay;
    }

    /// Where the next connection goes: the session's resume URL while it can
    /// be resumed, else @p gatewayUrl.
    std::string connectUrl(const std::string& gatewayUrl) const;
    bool ready() const
    {
        return m_ready;
    }
    const std::string& userName() const
    {
        return m_userName;
    }
    const std::string& userId() const
    {
        return m_userId;
    }

    std::deque<GatewayEvent> takeEvents();

private:
    void reconnect(std::string reason, bool keepSession);
    std::string heartbeat() const;

    std::string m_token;
    std::string m_name;
    Random m_random;
    std::string m_sessionId;
    std::string m_resumeUrl;
    std::optional<long long> m_sequence;
    std::chrono::milliseconds m_interval{41250};
    TimePoint m_connectedAt{};
    std::optional<TimePoint> m_nextHeartbeat;
    bool m_acknowledged = true;
    bool m_ready = false;
    bool m_reconnect = false;
    std::chrono::milliseconds m_delay{0};
    std::string m_reason;
    std::string m_fatal;
    std::string m_userName;
    std::string m_userId;
    std::deque<GatewayEvent> m_events;
};

} // namespace Bots
