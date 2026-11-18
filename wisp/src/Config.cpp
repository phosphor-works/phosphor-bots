// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>

extern char** environ;

namespace Wisp {

namespace {

constexpr std::string_view Prefix = "WISP_";

const std::set<std::string, std::less<>> KnownKeys = {
    "WISP_TOKEN",        "WISP_ORG",           "WISP_REPOS",          "WISP_ALIASES",
    "WISP_DEFAULT_REPO", "WISP_CHANNEL_REPOS", "WISP_CHANNELS",       "WISP_CARDS_PRS",
    "WISP_CARDS_ISSUES", "WISP_FEED",          "WISP_STATE",          "WISP_POLL_SECONDS",
    "WISP_CLOSED_HOURS", "WISP_REPLY_DAYS",    "WISP_MAX_REFERENCES", "WISP_ICON_URL",
};

[[noreturn]] void fail(std::string_view key, std::string_view why)
{
    throw std::runtime_error(std::format("{}: {}", key, why));
}

std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '\t'))
            ++i;
        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != ',' && text[i] != '\t')
            ++i;
        if (i > start)
            out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

bool repoName(std::string_view name)
{
    if (name.empty() || name.size() > 100)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.';
    });
}

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return out;
}

Snowflake channel(std::string_view key, std::string_view text)
{
    const auto id = parseSnowflake(text);
    if (!id)
        fail(key, std::format("\"{}\" is not a channel id (Developer Mode, right click, Copy Channel ID)", text));
    return *id;
}

long number(std::string_view key, std::string_view text, long low, long high)
{
    long value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < low || value > high)
        fail(key, std::format("\"{}\" is not a number from {} to {}", text, low, high));
    return value;
}

} // namespace

std::map<std::string, std::string> processEnvironment()
{
    std::map<std::string, std::string> out;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view text(*entry);
        const auto equals = text.find('=');
        if (equals != std::string_view::npos)
            out.emplace(std::string(text.substr(0, equals)), std::string(text.substr(equals + 1)));
    }
    return out;
}

Config parseConfig(const std::map<std::string, std::string>& env)
{
    for (const auto& [key, value] : env) {
        if (key.starts_with(Prefix) && !KnownKeys.contains(key))
            fail(key, "no such setting");
    }
    const auto get = [&](std::string_view key) -> std::string {
        const auto it = env.find(std::string(key));
        return it == env.end() ? std::string() : it->second;
    };

    Config config;
    config.token = get("WISP_TOKEN");
    if (config.token.empty())
        fail("WISP_TOKEN", "needed: the Discord bot's token");
    config.githubToken = get("GITHUB_TOKEN");

    if (const auto org = get("WISP_ORG"); !org.empty()) {
        if (!repoName(org))
            fail("WISP_ORG", std::format("\"{}\" is not an organization's name", org));
        config.org = org;
    }
    for (auto& repo : words(get("WISP_REPOS"))) {
        if (!repoName(repo))
            fail("WISP_REPOS", std::format("\"{}\" is not a repository's name", repo));
        config.repos.push_back(std::move(repo));
    }
    for (const auto& pair : words(get("WISP_ALIASES"))) {
        const auto equals = pair.find('=');
        if (equals == std::string::npos || !repoName(pair.substr(0, equals)) || !repoName(pair.substr(equals + 1)))
            fail("WISP_ALIASES", std::format("\"{}\" is not alias=Repository", pair));
        config.aliases[lower(pair.substr(0, equals))] = pair.substr(equals + 1);
    }
    if (const auto repo = get("WISP_DEFAULT_REPO"); !repo.empty()) {
        if (!repoName(repo))
            fail("WISP_DEFAULT_REPO", std::format("\"{}\" is not a repository's name", repo));
        config.defaultRepo = repo;
    }
    for (const auto& pair : words(get("WISP_CHANNEL_REPOS"))) {
        const auto equals = pair.find('=');
        if (equals == std::string::npos || !repoName(pair.substr(equals + 1)))
            fail("WISP_CHANNEL_REPOS", std::format("\"{}\" is not channel=Repository", pair));
        config.channelRepos[channel("WISP_CHANNEL_REPOS", pair.substr(0, equals))] = pair.substr(equals + 1);
    }
    for (const auto& id : words(get("WISP_CHANNELS")))
        config.channels.insert(channel("WISP_CHANNELS", id));

    const auto optionalChannel = [&](std::string_view key) -> std::optional<Snowflake> {
        const auto value = get(key);
        if (value.empty())
            return std::nullopt;
        return channel(key, value);
    };
    config.cardsPulls = optionalChannel("WISP_CARDS_PRS");
    config.cardsIssues = optionalChannel("WISP_CARDS_ISSUES");
    config.feed = optionalChannel("WISP_FEED");
    if (config.cardsWanted() && config.githubToken.empty())
        fail("GITHUB_TOKEN", "needed by the cards and the feed (60 questions an hour without it are too few)");

    config.statePath = get("WISP_STATE");
    config.iconUrl = get("WISP_ICON_URL");
    if (!config.iconUrl.empty() && !config.iconUrl.starts_with("https://"))
        fail("WISP_ICON_URL", "an address that starts with https://");
    if (const auto value = get("WISP_POLL_SECONDS"); !value.empty())
        config.pollInterval = std::chrono::seconds(number("WISP_POLL_SECONDS", value, 10, 3600));
    if (const auto value = get("WISP_CLOSED_HOURS"); !value.empty())
        config.closedKeep = std::chrono::hours(number("WISP_CLOSED_HOURS", value, 0, 24 * 30));
    if (const auto value = get("WISP_REPLY_DAYS"); !value.empty())
        config.replyFollow = std::chrono::hours(24 * number("WISP_REPLY_DAYS", value, 0, 30));
    if (const auto value = get("WISP_MAX_REFERENCES"); !value.empty())
        config.maxReferences = std::size_t(number("WISP_MAX_REFERENCES", value, 1, 10));
    return config;
}

} // namespace Wisp
