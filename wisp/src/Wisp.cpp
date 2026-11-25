// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Wisp.h"

#include "Embeds.h"
#include "Gateway.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>

namespace Wisp {

using Json = nlohmann::json;

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr auto ReplyRefresh = std::chrono::minutes(10);
constexpr auto RepoRefresh = std::chrono::hours(1);

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return out;
}

std::string userAgent()
{
    return std::format("DiscordBot ({}, {}) wisp", HomePage, Version);
}

Snowflake idOf(const Json& object, const char* key)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_string())
        return 0;
    return parseSnowflake(object[key].get<std::string>()).value_or(0);
}

} // namespace

Bot::Bot(Config config, HttpTransport discordTransport, HttpTransport githubTransport)
    : m_config(std::move(config))
    , m_discord(m_config.token, userAgent(), std::move(discordTransport))
    , m_github(m_config.org, m_config.githubToken, std::format("wisp/{} (+{})", Version, HomePage),
               std::move(githubTransport))
    , m_cards(m_config, m_github, m_discord, m_state)
{
    if (!m_config.statePath.empty()) {
        if (auto loaded = loadState(m_config.statePath)) {
            m_state = std::move(*loaded);
            m_stateLoaded = true;
        }
    }
    for (const auto& repo : m_config.repos)
        m_knownRepos[lower(repo)] = repo;
}

std::int64_t Bot::now() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void Bot::post(GatewayEvent event)
{
    {
        const std::lock_guard lock(m_mutex);
        m_events.push_back(std::move(event));
    }
    m_wake.notify_one();
}

void Bot::stop()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
}

int Bot::run()
{
    Gateway gateway(
        m_config.token, "wisp", userAgent(),
        [this](GatewayEvent event) {
            post(std::move(event));
        },
        [this](const std::string& reason) {
            {
                const std::lock_guard lock(m_mutex);
                m_fatal = reason;
                m_stop = true;
            }
            m_wake.notify_one();
        });
    gateway.start();
    refreshKnownRepos();

    auto nextCards = SteadyClock::now();
    auto nextReplies = SteadyClock::now() + ReplyRefresh;
    auto nextRepos = SteadyClock::now() + RepoRefresh;
    while (true) {
        std::deque<GatewayEvent> events;
        {
            std::unique_lock lock(m_mutex);
            const auto deadline =
                std::min({nextCards, nextReplies, nextRepos, SteadyClock::now() + std::chrono::seconds(1)});
            m_wake.wait_until(lock, deadline, [this] {
                return !m_events.empty() || m_stop;
            });
            if (m_stop)
                break;
            events.swap(m_events);
        }
        for (const auto& event : events)
            handle(event);

        const auto now = SteadyClock::now();
        if (m_config.cardsWanted() && m_botUser && now >= nextCards) {
            cardsRound();
            nextCards = SteadyClock::now() + m_config.pollInterval;
        }
        if (now >= nextReplies) {
            refreshReplies();
            nextReplies = now + ReplyRefresh;
        }
        if (now >= nextRepos) {
            refreshKnownRepos();
            nextRepos = now + RepoRefresh;
        }
    }
    gateway.stop();
    save();
    if (!m_fatal.empty()) {
        Log::info("wisp", "stopped: {}", m_fatal);
        return 1;
    }
    return 0;
}

void Bot::handle(const GatewayEvent& event)
{
    if (event.type == "READY") {
        const Json& user = event.data.contains("user") ? event.data["user"] : Json();
        m_botUser = idOf(user, "id");
        // Wisp's avatar is the bot's own (the mascot, set in the Developer
        // Portal), unless WISP_ICON_URL names another.
        if (m_config.iconUrl.empty() && m_botUser && user.contains("avatar") && user["avatar"].is_string()) {
            Embeds::Look look = m_cards.look();
            look.iconUrl = std::format("https://cdn.discordapp.com/avatars/{}/{}.png?size=64", m_botUser,
                                       user["avatar"].get<std::string>());
            m_cards.setLook(std::move(look));
        }
        Log::info("wisp", "up as {} for {}; replies in {}; cards: pull requests {}, issues {}, feed {}",
                  user.is_object() ? user.value("username", "?") : "?", m_config.org,
                  m_config.channels.empty() ? std::string("every channel")
                                            : std::format("{} channels", m_config.channels.size()),
                  m_config.cardsPulls ? std::to_string(*m_config.cardsPulls) : "off",
                  m_config.cardsIssues ? std::to_string(*m_config.cardsIssues) : "off",
                  m_config.feed ? std::to_string(*m_config.feed) : "off");
    } else if (event.type == "MESSAGE_CREATE") {
        message(event.data);
    } else if (event.type == "MESSAGE_DELETE") {
        messageDeleted(event.data);
    }
}

void Bot::refreshKnownRepos()
{
    if (auto avatar = m_github.avatarUrl(); avatar && *avatar != m_cards.look().orgIconUrl) {
        Embeds::Look look = m_cards.look();
        look.orgIconUrl = std::move(*avatar);
        m_cards.setLook(std::move(look));
    }
    if (!m_cards.refreshRepositories())
        return;
    for (const auto& repo : m_cards.repositories())
        m_knownRepos[lower(repo.name)] = repo.name;
}

std::optional<std::string> Bot::resolveRepo(std::string_view name) const
{
    std::string key = lower(name);
    if (const auto alias = m_config.aliases.find(key); alias != m_config.aliases.end())
        key = lower(alias->second);
    if (const auto known = m_knownRepos.find(key); known != m_knownRepos.end())
        return known->second;
    return std::nullopt;
}

std::vector<Item> Bot::lookUp(const std::vector<Reference>& references)
{
    std::vector<Item> items;
    for (const auto& reference : references) {
        if (auto item = m_github.item(reference.repo, reference.number))
            items.push_back(std::move(*item));
    }
    return items;
}

void Bot::message(const Json& data)
{
    if (!data.is_object() || data.contains("webhook_id"))
        return;
    if (data.contains("author") && data["author"].is_object() && data["author"].value("bot", false))
        return;
    const Snowflake channel = idOf(data, "channel_id");
    const Snowflake source = idOf(data, "id");
    if (!channel || !source || (!m_config.channels.empty() && !m_config.channels.contains(channel)))
        return;
    const std::string content = data.value("content", "");
    if (content.find('#') == std::string::npos)
        return;

    std::optional<std::string> bare;
    if (const auto it = m_config.channelRepos.find(channel); it != m_config.channelRepos.end())
        bare = resolveRepo(it->second);
    else if (!m_config.defaultRepo.empty())
        bare = resolveRepo(m_config.defaultRepo);
    const auto references = findReferences(
        content,
        [this](std::string_view name) {
            return resolveRepo(name);
        },
        m_config.org, bare, m_config.maxReferences);
    if (references.empty())
        return;
    const auto items = lookUp(references);
    if (items.empty())
        return;

    const Json reply = Embeds::reply(m_cards.look(), items);
    const DiscordResult result = m_discord.send(channel, reply, source);
    const Snowflake id = idOf(result.body, "id");
    if (!result.ok() || !id) {
        Log::info("wisp", "could not reply in channel {}: {}", channel, result.status);
        return;
    }
    m_state.replies.push_back({channel, id, source, references, Cards::hash(reply), now()});
    save();
}

void Bot::messageDeleted(const Json& data)
{
    const Snowflake source = idOf(data, "id");
    if (!source)
        return;
    const auto it = std::find_if(m_state.replies.begin(), m_state.replies.end(), [&](const ReplyRecord& reply) {
        return reply.source == source;
    });
    if (it == m_state.replies.end())
        return;
    // A question taken back takes its answer with it.
    m_discord.remove(it->channel, it->message);
    m_state.replies.erase(it);
    save();
}

void Bot::refreshReplies()
{
    const std::int64_t cutoff = now() - std::int64_t(std::chrono::seconds(m_config.replyFollow).count());
    std::erase_if(m_state.replies, [&](const ReplyRecord& reply) {
        return reply.at < cutoff;
    });
    for (auto it = m_state.replies.begin(); it != m_state.replies.end();) {
        const auto items = lookUp(it->references);
        if (items.size() != it->references.size()) {
            ++it; // GitHub did not answer for each: as it was
            continue;
        }
        const Json reply = Embeds::reply(m_cards.look(), items);
        const std::string drawn = Cards::hash(reply);
        if (drawn != it->drawn) {
            const DiscordResult result = m_discord.edit(it->channel, it->message, reply);
            if (result.status == 404) {
                it = m_state.replies.erase(it);
                continue;
            }
            if (result.ok())
                it->drawn = drawn;
        }
        ++it;
    }
    save();
}

void Bot::cardsRound()
{
    if (!m_stateLoaded && !m_cleaned && m_botUser) {
        m_cards.cleanUp(m_botUser);
        m_cleaned = true;
    }
    m_cards.round();
    save();
}

void Bot::save()
{
    if (!m_config.statePath.empty() && saveState(m_config.statePath, m_state))
        m_stateLoaded = true;
}

} // namespace Wisp
