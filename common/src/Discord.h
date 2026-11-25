// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Http.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Bots {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using Snowflake = std::uint64_t;

/// A snowflake from its decimal text, or nothing when it is not one.
std::optional<Snowflake> parseSnowflake(std::string_view text);

/// The answer of one call of Discord's REST API.
struct DiscordResult
{
    long status = 0;
    Json body;
    std::string error;
    bool ok() const
    {
        return status >= 200 && status < 300;
    }
};

/// Discord's REST API, v10, for one thread. It waits as Discord's rate limits
/// say: a route's bucket when its X-RateLimit-Remaining reaches 0, and the
/// Retry-After of a 429 (each route, or every route for a global one).
class Discord
{
public:
    using Sleep = std::function<void(std::chrono::milliseconds)>;

    /// @p userAgent: "name/version (+home page)", in Discord's DiscordBot form.
    Discord(std::string token, std::string userAgent, HttpTransport transport, Sleep sleep = {});

    DiscordResult call(const std::string& method, const std::string& path, const Json& body = nullptr);

    /// POST a message; @p replyTo makes it a reply that pings no one.
    DiscordResult send(Snowflake channel, Json message, std::optional<Snowflake> replyTo = std::nullopt);
    DiscordResult edit(Snowflake channel, Snowflake message, const Json& content);
    DiscordResult remove(Snowflake channel, Snowflake message);
    /// Up to 100 messages before @p before (0: the newest).
    DiscordResult messages(Snowflake channel, Snowflake before = 0);
    /// GET /gateway/bot: where to connect, and how many sessions are left.
    DiscordResult gatewayBot();

    /// The route a request counts against: the path with each id but the
    /// channel's (and the webhook's, the guild's) replaced, as Discord buckets
    /// them. Public for the tests.
    static std::string routeKey(const std::string& method, const std::string& path);

private:
    std::string m_token;
    std::string m_userAgent;
    HttpTransport m_transport;
    Sleep m_sleep;
    std::map<std::string, Clock::time_point> m_routeReady;
    Clock::time_point m_globalReady{};
};

} // namespace Bots
