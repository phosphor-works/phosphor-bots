// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Discord.h"

#include "Log.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <thread>

namespace Bots {

namespace {

constexpr std::string_view Api = "https://discord.com/api/v10";
constexpr int MaxTries = 4;

std::chrono::milliseconds secondsText(const std::string& text)
{
    // strtod: libc++ before LLVM 20 (FreeBSD 15's) has no float from_chars.
    // The program never sets a locale, so the decimal point is '.'.
    char* end = nullptr;
    const double seconds = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(seconds) || seconds < 0)
        return std::chrono::milliseconds(0);
    // A limit Discord gives is never long; a nonsense one must not stop us for good.
    return std::chrono::milliseconds(long(std::min(seconds, 600.0) * 1000.0 + 0.5));
}

bool allDigits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

} // namespace

std::optional<Snowflake> parseSnowflake(std::string_view text)
{
    Snowflake value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size() || value == 0)
        return std::nullopt;
    return value;
}

Discord::Discord(std::string token, std::string userAgent, HttpTransport transport, Sleep sleep)
    : m_token(std::move(token))
    , m_userAgent(std::move(userAgent))
    , m_transport(std::move(transport))
    , m_sleep(sleep ? std::move(sleep) : Sleep([](std::chrono::milliseconds d) {
        std::this_thread::sleep_for(d);
    }))
{
}

std::string Discord::routeKey(const std::string& method, const std::string& path)
{
    // Discord's major parameters (channel, guild, webhook) keep their id: each
    // is a bucket of its own. Other ids are one route.
    std::string key = method + " ";
    std::string_view rest = path;
    std::string_view previous;
    while (!rest.empty()) {
        if (rest.front() == '/')
            rest.remove_prefix(1);
        const auto slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
        const auto query = part.find('?');
        const std::string_view segment = part.substr(0, query);
        key += '/';
        if (allDigits(segment) && previous != "channels" && previous != "guilds" && previous != "webhooks")
            key += "{id}";
        else
            key += segment;
        previous = segment;
    }
    // A message's deletion has its own limit.
    if (method == "DELETE" && allDigits(previous))
        key += "#delete";
    return key;
}

DiscordResult Discord::call(const std::string& method, const std::string& path, const Json& body)
{
    const std::string route = routeKey(method, path);
    HttpRequest request;
    request.method = method;
    request.url = std::string(Api) + path;
    request.headers = {
        "Authorization: Bot " + m_token,
        "User-Agent: " + m_userAgent,
    };
    if (!body.is_null()) {
        request.headers.emplace_back("Content-Type: application/json");
        request.body = body.dump(-1, ' ', false, Json::error_handler_t::replace);
    }

    DiscordResult result;
    for (int attempt = 0; attempt < MaxTries; ++attempt) {
        const auto now = Clock::now();
        auto ready = std::max(m_globalReady, m_routeReady[route]);
        if (ready > now)
            m_sleep(std::chrono::duration_cast<std::chrono::milliseconds>(ready - now));

        const HttpResponse response = m_transport(request);
        result.status = response.status;
        result.error = response.error;
        result.body = response.body.empty() ? Json() : Json::parse(response.body, nullptr, false);
        if (result.body.is_discarded())
            result.body = Json();

        if (response.header("x-ratelimit-remaining") == "0")
            m_routeReady[route] = Clock::now() + secondsText(response.header("x-ratelimit-reset-after"));

        if (response.status == 429) {
            std::chrono::milliseconds wait = secondsText(response.header("retry-after"));
            if (result.body.is_object() && result.body.contains("retry_after")
                && result.body["retry_after"].is_number())
                wait = secondsText(std::to_string(result.body["retry_after"].get<double>()));
            const bool global = response.header("x-ratelimit-global") == "true"
                || (result.body.is_object() && result.body.value("global", false));
            (global ? m_globalReady : m_routeReady[route]) = Clock::now() + wait;
            Log::info("discord", "{} {}: rate limited, waiting {} ms{}", method, route, wait.count(),
                      global ? " (global)" : "");
            continue;
        }
        if (response.status >= 500 || response.status == 0) {
            // Discord's own trouble, or the network's: a short wait, then again.
            Log::info("discord", "{} {}: {} {}", method, route, response.status,
                      response.status == 0 ? response.error : std::string("server error"));
            m_sleep(std::chrono::milliseconds(1000 << attempt));
            continue;
        }
        return result;
    }
    return result;
}

DiscordResult Discord::send(Snowflake channel, Json message, std::optional<Snowflake> replyTo)
{
    message["allowed_mentions"] = {{"parse", Json::array()}, {"replied_user", false}};
    if (replyTo)
        message["message_reference"] = {{"message_id", std::to_string(*replyTo)}, {"fail_if_not_exists", false}};
    return call("POST", std::format("/channels/{}/messages", channel), message);
}

DiscordResult Discord::edit(Snowflake channel, Snowflake message, const Json& content)
{
    Json body = content;
    body["allowed_mentions"] = {{"parse", Json::array()}, {"replied_user", false}};
    return call("PATCH", std::format("/channels/{}/messages/{}", channel, message), body);
}

DiscordResult Discord::remove(Snowflake channel, Snowflake message)
{
    return call("DELETE", std::format("/channels/{}/messages/{}", channel, message));
}

DiscordResult Discord::messages(Snowflake channel, Snowflake before)
{
    if (before)
        return call("GET", std::format("/channels/{}/messages?limit=100&before={}", channel, before));
    return call("GET", std::format("/channels/{}/messages?limit=100", channel));
}

DiscordResult Discord::gatewayBot()
{
    return call("GET", "/gateway/bot");
}

} // namespace Bots
