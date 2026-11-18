// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glim.h"

#include "Gateway.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

namespace Glim {

using Json = nlohmann::json;

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr auto ReposRefresh = std::chrono::hours(1);
constexpr int MaxPages = 10; ///< 1000 messages of a post at most

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
    return std::format("DiscordBot ({}, {}) glim", HomePage, Version);
}

Snowflake idOf(const Json& object, const char* key)
{
    if (!object.is_object() || !object.contains(key) || !object[key].is_string())
        return 0;
    return parseSnowflake(object[key].get<std::string>()).value_or(0);
}

std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

std::string firstWord(std::string_view text)
{
    const std::string trimmed = trim(text);
    return lower(trimmed.substr(0, trimmed.find_first_of(" \t\n")));
}

constexpr std::string_view Usage =
    "Mention me in a post of the help forum to file it as a GitHub issue: `@Glim` (the post's tag picks the "
    "repository), or `@Glim afterglow`. Mention me again later to add what is new in the post. Only the post's "
    "author and staff can.";

} // namespace

Bot::Bot(Config config, HttpTransport discordTransport, HttpTransport githubTransport, GitHub::TokenSource githubToken)
    : m_config(std::move(config))
    , m_discord(m_config.token, userAgent(), discordTransport)
    , m_fetch(discordTransport)
    , m_github(m_config.org, std::move(githubToken), std::format("glim/{} (+{})", Version, HomePage),
               std::move(githubTransport))
{
    if (!m_config.statePath.empty()) {
        if (auto loaded = loadState(m_config.statePath))
            m_state = std::move(*loaded);
    }
    for (const auto& map : {m_config.tagRepos, m_config.aliases}) {
        for (const auto& [name, repo] : map)
            m_knownRepos[lower(repo)] = repo;
    }
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
        m_config.token, "glim", userAgent(),
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
    auto nextRound = SteadyClock::now() + std::chrono::seconds(30);
    while (true) {
        std::deque<GatewayEvent> events;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait_until(lock, std::min(nextRound, SteadyClock::now() + std::chrono::seconds(1)), [this] {
                return !m_events.empty() || m_stop;
            });
            if (m_stop)
                break;
            events.swap(m_events);
        }
        for (const auto& event : events)
            handle(event);
        if (SteadyClock::now() >= nextRound) {
            round();
            nextRound = SteadyClock::now() + m_config.pollInterval;
        }
    }
    gateway.stop();
    save();
    if (!m_fatal.empty()) {
        Log::info("glim", "stopped: {}", m_fatal);
        return 1;
    }
    return 0;
}

void Bot::handle(const GatewayEvent& event)
{
    if (event.type == "READY") {
        const Json& user = event.data.contains("user") ? event.data["user"] : Json();
        m_user = idOf(user, "id");
        if (user.is_object() && user.contains("avatar") && user["avatar"].is_string())
            m_avatar = std::format("https://cdn.discordapp.com/avatars/{}/{}.png?size=64", m_user,
                                   user["avatar"].get<std::string>());
        Log::info("glim", "up as {} for {}; files posts of {} forums; {} posts followed",
                  user.is_object() ? user.value("username", "?") : "?", m_config.org, m_config.forums.size(),
                  m_state.posts.size());
    } else if (event.type == "MESSAGE_CREATE") {
        mentioned(event.data);
    }
}

std::string Bot::withoutMention(std::string_view content) const
{
    std::string out(content);
    for (const std::string& mention : {std::format("<@{}>", m_user), std::format("<@!{}>", m_user)}) {
        for (auto at = out.find(mention); at != std::string::npos; at = out.find(mention))
            out.erase(at, mention.size());
    }
    return out;
}

void Bot::mentioned(const Json& message)
{
    if (!m_user || !message.is_object() || message.contains("webhook_id"))
        return;
    if (message.contains("author") && message["author"].is_object() && message["author"].value("bot", false))
        return;
    // Only a written mention: a reply to Glim mentions it too, and is no request.
    const std::string content = message.value("content", "");
    if (content.find(std::format("<@{}>", m_user)) == std::string::npos
        && content.find(std::format("<@!{}>", m_user)) == std::string::npos)
        return;

    const Snowflake channel = idOf(message, "channel_id");
    const Snowflake id = idOf(message, "id");
    const DiscordResult thread = m_discord.call("GET", std::format("/channels/{}", channel));
    if (!thread.ok() || !thread.body.is_object())
        return;
    const Snowflake parent = idOf(thread.body, "parent_id");
    const int type = thread.body.value("type", -1);
    if ((type != 11 && type != 12) || !m_config.forums.contains(parent)) {
        reply(channel, id, std::format("I file posts of <#{}> as GitHub issues. {}", *m_config.forums.begin(), Usage));
        return;
    }

    const std::string words = trim(withoutMention(content));
    if (firstWord(words) == "help") {
        reply(channel, id, std::string(Usage));
        return;
    }

    // The post's author, or staff.
    const Snowflake author = message.contains("author") ? idOf(message["author"], "id") : 0;
    bool allowed = author && author == idOf(thread.body, "owner_id");
    if (!allowed && message.contains("member") && message["member"].contains("roles")) {
        for (const auto& role : message["member"]["roles"]) {
            if (role.is_string() && m_config.staffRoles.contains(parseSnowflake(role.get<std::string>()).value_or(0)))
                allowed = true;
        }
    }
    if (!allowed) {
        reply(channel, id, "Only the post's author and staff can ask me to file a post.");
        return;
    }
    file(message, thread.body, words);
}

std::optional<std::string> Bot::resolveRepo(std::string_view name)
{
    if (m_reposFetched == SteadyClock::time_point{} || SteadyClock::now() - m_reposFetched > ReposRefresh) {
        if (const auto repos = m_github.repositories()) {
            for (const auto& repo : *repos) {
                if (!repo.archived)
                    m_knownRepos[lower(repo.name)] = repo.name;
            }
            m_reposFetched = SteadyClock::now();
        }
    }
    std::string key = lower(name);
    if (const auto alias = m_config.aliases.find(key); alias != m_config.aliases.end())
        key = lower(alias->second);
    if (const auto tag = m_config.tagRepos.find(key); tag != m_config.tagRepos.end())
        key = lower(tag->second);
    if (const auto known = m_knownRepos.find(key); known != m_knownRepos.end())
        return known->second;
    return std::nullopt;
}

std::vector<std::string> Bot::tagNames(const Json& thread)
{
    const Snowflake forumId = idOf(thread, "parent_id");
    auto forum = m_forums.find(forumId);
    if (forum == m_forums.end()) {
        const DiscordResult result = m_discord.call("GET", std::format("/channels/{}", forumId));
        if (!result.ok())
            return {};
        forum = m_forums.emplace(forumId, result.body).first;
    }
    std::vector<std::string> out;
    if (!thread.contains("applied_tags") || !forum->second.contains("available_tags"))
        return out;
    for (const auto& applied : thread["applied_tags"]) {
        for (const auto& tag : forum->second["available_tags"]) {
            if (applied.is_string() && tag.value("id", "") == applied.get<std::string>())
                out.push_back(lower(tag.value("name", "")));
        }
    }
    return out;
}

std::optional<std::vector<Report::Message>> Bot::collect(Snowflake thread, Snowflake after, Snowflake* newest)
{
    std::vector<Json> raw;
    Snowflake cursor = after;
    for (int page = 0; page < MaxPages; ++page) {
        const DiscordResult result =
            m_discord.call("GET", std::format("/channels/{}/messages?limit=100&after={}", thread, cursor));
        if (!result.ok() || !result.body.is_array())
            return std::nullopt;
        for (const auto& message : result.body) {
            raw.push_back(message);
            cursor = std::max(cursor, idOf(message, "id"));
        }
        if (result.body.size() < 100)
            break;
    }
    std::sort(raw.begin(), raw.end(), [](const Json& a, const Json& b) {
        return idOf(a, "id") < idOf(b, "id");
    });
    *newest = cursor;

    std::vector<Report::Message> out;
    for (const auto& message : raw) {
        const Json& author = message.contains("author") ? message["author"] : Json();
        if (!author.is_object() || author.value("bot", false) || message.contains("webhook_id"))
            continue;
        Report::Message parsed{idOf(message, "id"),
                               idOf(author, "id"),
                               trim(withoutMention(message.value("content", ""))),
                               message.value("timestamp", ""),
                               {}};
        if (message.contains("attachments") && message["attachments"].is_array()) {
            for (const auto& file : message["attachments"]) {
                Report::Attachment attachment{file.value("filename", "file"), file.value("content_type", ""),
                                              file.value("size", 0LL), file.value("url", ""), std::nullopt};
                // Only a file small enough to be put whole is read whole;
                // a larger one is cut, never read past twice the limit.
                if (Report::isText(attachment) && attachment.url.starts_with("https://")
                    && std::size_t(attachment.size) <= 4 * 1024 * 1024) {
                    HttpRequest request;
                    request.url = attachment.url;
                    const HttpResponse response = m_fetch(request);
                    if (response.status == 200)
                        attachment.text = response.body;
                }
                parsed.attachments.push_back(std::move(attachment));
            }
        }
        if (parsed.content.empty() && parsed.attachments.empty())
            continue; // a bare mention of Glim
        out.push_back(std::move(parsed));
    }
    return out;
}

void Bot::file(const Json& message, const Json& thread, const std::string& words)
{
    const Snowflake channel = idOf(thread, "id");
    const Snowflake trigger = idOf(message, "id");
    const Snowflake requester = idOf(message["author"], "id");
    const Snowflake guild = idOf(thread, "guild_id");
    const std::string postUrl = std::format("https://discord.com/channels/{}/{}", guild, channel);
    const Report::Limits limits{m_config.textLimit, 60000};

    auto existing = m_state.posts.find(channel);
    if (existing != m_state.posts.end()) {
        PostRecord& record = existing->second;
        Snowflake newest = record.lastMessage;
        const auto messages = collect(channel, record.lastMessage, &newest);
        if (!messages) {
            reply(channel, trigger, "Discord did not give me the post's messages. Try again in a minute.");
            return;
        }
        if (messages->empty()) {
            tell(channel, trigger, std::format("{}#{}", record.repo, record.number), record.url,
                 "Nothing is new in this post since the issue was last updated.", Brand::Slate);
            record.lastMessage = newest;
            save();
            return;
        }
        Report::Participants participants = record.participants;
        const std::string body = Report::updateComment(postUrl, *messages, participants, requester, limits);
        const auto url = m_github.comment(record.repo, record.number, body);
        if (!url) {
            reply(channel, trigger, "GitHub did not take the update. A maintainer can find why in my log.");
            return;
        }
        record.participants = participants;
        record.lastMessage = newest;
        tell(channel, trigger, std::format("Updated {}#{}", record.repo, record.number), *url,
             std::format("{} new message{} added to the issue.", messages->size(), messages->size() == 1 ? "" : "s"),
             Brand::Cyan);
        save();
        return;
    }

    // The repository: named in the mention, else the post's tag.
    std::optional<std::string> repo;
    if (const std::string named = firstWord(words); !named.empty()) {
        repo = resolveRepo(named);
        if (!repo) {
            reply(channel, trigger, std::format("I know no repository called \"{}\". {}", named, Usage));
            return;
        }
    } else {
        std::set<std::string> repos;
        for (const auto& tag : tagNames(thread)) {
            if (const auto it = m_config.tagRepos.find(tag); it != m_config.tagRepos.end())
                repos.insert(it->second);
        }
        if (repos.size() != 1) {
            reply(channel, trigger,
                  repos.empty() ? "This post has no project tag. Add one, or name the repository: `@Glim afterglow`."
                                : "This post has more than one project tag. Name the repository: `@Glim afterglow`.");
            return;
        }
        repo = resolveRepo(*repos.begin());
        if (!repo)
            repo = *repos.begin();
    }

    Snowflake newest = 0;
    // A forum post's first message has the post's id: start just before it.
    const auto messages = collect(channel, channel - 1, &newest);
    if (!messages || messages->empty()) {
        reply(channel, trigger, "Discord did not give me the post's messages. Try again in a minute.");
        return;
    }
    Report::Participants participants;
    participants.reporter = idOf(thread, "owner_id");
    const std::string title = Report::cut(thread.value("name", "A post from Discord"), 256);
    const std::string body = Report::issueBody(postUrl, *messages, participants, requester, limits);
    const auto issue = m_github.createIssue(*repo, title, body, m_config.labels);
    if (!issue) {
        reply(channel, trigger, "GitHub did not take the issue. A maintainer can find why in my log.");
        return;
    }
    m_state.posts[channel] = {*repo, issue->number, issue->url, newest, participants, "open"};
    tell(channel, trigger, std::format("Filed as {}#{}", *repo, issue->number), issue->url,
         "Mention me again to add what is new in this post. I will say here when the issue closes.", Brand::Cyan);
    Log::info("glim", "filed post {} as {}#{}", channel, *repo, issue->number);
    save();
}

void Bot::round()
{
    std::vector<Snowflake> gone;
    for (auto& [thread, record] : m_state.posts) {
        bool deleted = false;
        const auto item = m_github.item(record.repo, record.number, &deleted);
        if (!item) {
            if (deleted)
                gone.push_back(thread);
            continue;
        }
        if (item->state == record.issueState)
            continue;
        const std::string name = std::format("{}#{}", record.repo, record.number);
        if (item->state == "closed") {
            const bool completed = item->pull ? item->merged : item->stateReason != "not_planned";
            if (tell(thread, std::nullopt, std::format("{} was closed", name), item->url,
                     completed ? "The issue is done." : "The issue was closed as not planned.",
                     completed ? Brand::Purple : Brand::Slate)
                == 404) {
                gone.push_back(thread); // the post was deleted
                continue;
            }
            if (completed && !m_config.solvedTag.empty())
                addTag(thread, m_config.solvedTag);
        } else {
            tell(thread, std::nullopt, std::format("{} was opened again", name), item->url, "", Brand::Cyan);
        }
        record.issueState = item->state;
    }
    for (const auto thread : gone)
        m_state.posts.erase(thread);
    save();
}

bool Bot::addTag(Snowflake thread, const std::string& tag)
{
    const DiscordResult current = m_discord.call("GET", std::format("/channels/{}", thread));
    if (!current.ok())
        return false;
    const Snowflake forumId = idOf(current.body, "parent_id");
    tagNames(current.body); // the forum's tags, cached
    const auto forum = m_forums.find(forumId);
    if (forum == m_forums.end() || !forum->second.contains("available_tags"))
        return false;
    std::string tagId;
    for (const auto& available : forum->second["available_tags"]) {
        if (lower(available.value("name", "")) == tag)
            tagId = available.value("id", "");
    }
    if (tagId.empty())
        return false;
    Json applied = current.body.value("applied_tags", Json::array());
    if (std::find(applied.begin(), applied.end(), tagId) != applied.end())
        return true;
    if (applied.size() >= 5)
        applied.erase(applied.begin()); // Discord keeps five at most
    applied.push_back(tagId);
    return m_discord.call("PATCH", std::format("/channels/{}", thread), Json{{"applied_tags", applied}}).ok();
}

void Bot::reply(Snowflake channel, Snowflake to, const std::string& text)
{
    m_discord.send(channel, Json{{"content", text}}, to);
}

long Bot::tell(Snowflake channel, std::optional<Snowflake> to, const std::string& title, const std::string& url,
               const std::string& description, int color)
{
    Json embed = {{"title", title}, {"url", url}, {"color", color}};
    if (!description.empty())
        embed["description"] = description;
    Json footer = {{"text", "Glim · " + m_config.org}};
    if (!m_avatar.empty())
        footer["icon_url"] = m_avatar;
    embed["footer"] = footer;
    return m_discord.send(channel, Json{{"embeds", Json::array({embed})}}, to).status;
}

void Bot::save()
{
    if (!m_config.statePath.empty())
        saveState(m_config.statePath, m_state);
}

} // namespace Glim
