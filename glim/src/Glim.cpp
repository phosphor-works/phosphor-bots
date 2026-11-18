// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glim.h"

#include "Files.h"
#include "Http.h"
#include "Gateway.h"
#include "Media.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <future>
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
    if (!m_config.accessTeam.empty()) {
        m_access = std::make_unique<AccessJwt>(m_config.accessTeam, m_config.accessAud, makeCurlTransport());
        m_verify = [this](const std::string& token, std::string* why) {
            return m_access->verify(token, why);
        };
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
    m_running = true;
    if (!m_config.staffListen.empty()) {
        m_server = std::make_unique<HttpServer>([this](const ServerRequest& request) {
            return staffRequest(request);
        });
        if (!m_server->start(m_config.staffListen))
            m_server.reset();
    }
    publish();
    auto nextRound = SteadyClock::now() + std::chrono::seconds(30);
    while (true) {
        std::deque<GatewayEvent> events;
        std::deque<std::function<void()>> jobs;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait_until(lock, std::min(nextRound, SteadyClock::now() + std::chrono::seconds(1)), [this] {
                return !m_events.empty() || !m_jobs.empty() || m_stop;
            });
            if (m_stop)
                break;
            events.swap(m_events);
            jobs.swap(m_jobs);
        }
        for (const auto& event : events)
            handle(event);
        for (auto& job : jobs)
            job();
        if (SteadyClock::now() >= nextRound) {
            round();
            nextRound = SteadyClock::now() + m_config.pollInterval;
        }
    }
    if (m_server)
        m_server->stop();
    m_running = false;
    // A staff request still waiting gets its answer: the bot is going.
    for (auto& job : m_jobs)
        job();
    m_jobs.clear();
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
    std::size_t budget = m_config.postLimit;
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
            int index = 0;
            for (const auto& file : message["attachments"]) {
                Report::Attachment attachment{file.value("filename", "file"),
                                              file.value("content_type", ""),
                                              file.value("size", 0LL),
                                              file.value("url", ""),
                                              std::nullopt,
                                              {},
                                              {}};
                keep(thread, parsed.id, ++index, attachment, budget);
                parsed.attachments.push_back(std::move(attachment));
            }
        }
        if (parsed.content.empty() && parsed.attachments.empty())
            continue; // a bare mention of Glim
        out.push_back(std::move(parsed));
    }
    return out;
}

void Bot::keep(Snowflake thread, Snowflake message, int index, Report::Attachment& attachment, std::size_t& budget)
{
    const bool textName = Report::isText(attachment);
    const std::size_t limit = m_config.filesDir.empty() ? std::size_t(4) << 20 : m_config.fileLimit;
    // Without a folder for files only text is read, to be put in the issue.
    if ((!textName && m_config.filesDir.empty()) || !attachment.url.starts_with("https://") || attachment.size <= 0
        || std::size_t(attachment.size) > std::min(limit, budget))
        return;
    HttpRequest request;
    request.url = attachment.url;
    const HttpResponse response = m_fetch(request);
    if (response.status != 200 || response.body.size() > std::min(limit, budget))
        return;
    budget -= response.body.size();

    const auto type = Media::sniff(response.body, textName);
    if (!type)
        return; // an unknown kind of file: named only
    auto bytes = Media::strip(response.body, *type);
    if (!bytes) {
        Log::info("glim", "post {}: {} is no whole {}; named only", thread, index, type->extension);
        return;
    }
    if (type->kind == Media::Kind::Text)
        attachment.text = *bytes;
    if (m_config.filesDir.empty())
        return;

    // The name in the address says nothing of the file's own name.
    const std::string folder = std::format("{}/{}", m_config.filesDir, thread);
    const std::string name = std::format("{}-{}.{}", message, index, type->extension);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::filesystem::permissions(folder, std::filesystem::perms(0755), error);
    if (!writeFileAtomically(folder + "/" + name, *bytes, "files", 0644))
        return;
    attachment.publicUrl = std::format("{}/{}/{}", m_config.filesUrl, thread, name);
    attachment.keptPath = std::format("{}/{}", thread, name);
    attachment.size = (long long)bytes->size();
    switch (type->kind) {
    case Media::Kind::Image:
        attachment.kind = "image";
        break;
    case Media::Kind::Video:
        attachment.kind = "video";
        break;
    case Media::Kind::Text:
        attachment.kind = "text";
        break;
    case Media::Kind::Archive:
        attachment.kind = "file";
        break;
    }
}

std::string Bot::update(Snowflake channel, Snowflake requester, std::optional<Snowflake> replyTo)
{
    auto existing = m_state.posts.find(channel);
    if (existing == m_state.posts.end())
        return "This post is not filed.";
    PostRecord& record = existing->second;
    const Report::Limits limits{m_config.textLimit, 60000};
    Snowflake newest = record.lastMessage;
    const auto messages = collect(channel, record.lastMessage, &newest);
    if (!messages) {
        if (replyTo)
            reply(channel, *replyTo, "Discord did not give me the post's messages. Try again in a minute.");
        return "Discord did not give the post's messages.";
    }
    if (messages->empty()) {
        if (replyTo)
            tell(channel, replyTo, std::format("{}#{}", record.repo, record.number), record.url,
                 "Nothing is new in this post since the issue was last updated.", Brand::Slate);
        record.lastMessage = newest;
        save();
        return "Nothing is new in the post.";
    }
    Report::Participants participants = record.participants;
    const std::string body = Report::updateComment(record.discordUrl, *messages, participants, requester, limits);
    const auto url = m_github.comment(record.repo, record.number, body);
    if (!url) {
        if (replyTo)
            reply(channel, *replyTo, "GitHub did not take the update. A maintainer can find why in my log.");
        return "GitHub did not take the update.";
    }
    record.participants = participants;
    record.lastMessage = newest;
    record.updatedAt = now();
    keepFiles(record, *messages);
    const std::string added =
        std::format("{} new message{} added to the issue.", messages->size(), messages->size() == 1 ? "" : "s");
    tell(channel, replyTo, std::format("Updated {}#{}", record.repo, record.number), *url, added, Brand::Cyan);
    save();
    return added;
}

void Bot::keepFiles(PostRecord& record, const std::vector<Report::Message>& messages)
{
    for (const auto& message : messages) {
        for (const auto& attachment : message.attachments) {
            if (!attachment.publicUrl.empty())
                record.files.push_back({attachment.name, attachment.kind, attachment.size, attachment.publicUrl,
                                        attachment.keptPath, false});
        }
    }
}

void Bot::file(const Json& message, const Json& thread, const std::string& words)
{
    const Snowflake channel = idOf(thread, "id");
    const Snowflake trigger = idOf(message, "id");
    const Snowflake requester = idOf(message["author"], "id");
    const Snowflake guild = idOf(thread, "guild_id");
    const std::string postUrl = std::format("https://discord.com/channels/{}/{}", guild, channel);
    const Report::Limits limits{m_config.textLimit, 60000};

    if (m_state.posts.contains(channel)) {
        update(channel, requester, trigger);
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
    PostRecord& record = m_state.posts[channel];
    record = {*repo, issue->number, issue->url, newest, participants, "open", title, now(), now(), postUrl, {}};
    keepFiles(record, *messages);
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
    publish();
}

std::int64_t Bot::now() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

namespace {

Json publicPost(Snowflake id, const PostRecord& post)
{
    return {{"id", std::to_string(id)},   {"repo", post.repo},
            {"number", post.number},      {"url", post.url},
            {"title", post.title},        {"state", post.issueState},
            {"filed", post.filedAt},      {"updated", post.updatedAt},
            {"discord", post.discordUrl}, {"files", filesJson(post.files, true)}};
}

} // namespace

void Bot::publish()
{
    if (m_config.filesDir.empty())
        return;
    // What the page shows names no one: the issue's own title, its state,
    // its dates, the kept files. The posts' ids are in the files' addresses
    // already.
    const std::string api = m_config.filesDir + "/api";
    std::error_code error;
    std::filesystem::create_directories(api + "/p", error);
    std::filesystem::permissions(api, std::filesystem::perms(0755), error);
    std::filesystem::permissions(api + "/p", std::filesystem::perms(0755), error);
    Json posts = Json::array();
    std::set<std::string> current;
    for (const auto& [id, post] : m_state.posts) {
        Json entry = publicPost(id, post);
        writeFileAtomically(std::format("{}/p/{}.json", api, id), entry.dump() + "\n", "files", 0644);
        current.insert(std::format("{}.json", id));
        entry["files"] = std::count_if(post.files.begin(), post.files.end(), [](const KeptFile& file) {
            return !file.removed;
        });
        posts.push_back(std::move(entry));
    }
    std::sort(posts.begin(), posts.end(), [](const Json& a, const Json& b) {
        return a["updated"] > b["updated"];
    });
    writeFileAtomically(api + "/posts.json",
                        Json{{"org", m_config.org}, {"generated", now()}, {"posts", posts}}.dump() + "\n", "files",
                        0644);
    for (const auto& entry : std::filesystem::directory_iterator(api + "/p", error)) {
        if (!current.contains(entry.path().filename().string()))
            std::filesystem::remove(entry.path(), error); // an unlinked post
    }
}

ServerResponse Bot::onBotThread(std::function<ServerResponse()> job)
{
    if (!m_running)
        return job();
    auto task = std::make_shared<std::packaged_task<ServerResponse()>>(std::move(job));
    auto result = task->get_future();
    {
        const std::lock_guard lock(m_mutex);
        if (m_stop)
            return {503, "application/json", R"({"error":"Glim is stopping"})"};
        m_jobs.push_back([task] {
            (*task)();
        });
    }
    m_wake.notify_one();
    if (result.wait_for(std::chrono::minutes(3)) != std::future_status::ready)
        return {503, "application/json", R"({"error":"Glim is busy; try again"})"};
    return result.get();
}

ServerResponse Bot::staffRequest(const ServerRequest& request)
{
    const auto error = [](int status, const std::string& message) {
        return ServerResponse{status, "application/json", Json{{"error", message}}.dump()};
    };
    if (!request.path.starts_with("/staff/api/"))
        return error(404, "no such page");
    // Cloudflare Access signs each request it lets through; one without its
    // token came some other way.
    const auto token = request.headers.find("cf-access-jwt-assertion");
    std::string why;
    const auto email =
        m_verify && token != request.headers.end() ? m_verify(token->second, &why) : std::optional<std::string>();
    if (!email) {
        Log::info("staff", "refused {} {}: {}", request.method, request.path,
                  token == request.headers.end() ? "no Access token" : why);
        return error(403, "Sign in through Cloudflare Access.");
    }
    if (!m_config.staffEmails.empty() && !m_config.staffEmails.contains(lower(*email)))
        return error(403, "This account is not on Glim's staff list.");
    // A form of another site cannot set this header: no request of a
    // signed-in browser is made for them.
    if (request.method == "POST" && !request.headers.contains("x-glim-staff"))
        return error(400, "the X-Glim-Staff header is missing");
    return onBotThread([this, request, email = *email] {
        return staffAction(request, email);
    });
}

ServerResponse Bot::staffAction(const ServerRequest& request, const std::string& email)
{
    const auto answer = [](int status, const Json& body) {
        return ServerResponse{status, "application/json", body.dump()};
    };
    std::vector<std::string> parts;
    for (std::size_t at = std::string("/staff/api/").size(); at <= request.path.size();) {
        const auto slash = request.path.find('/', at);
        parts.push_back(request.path.substr(at, slash == std::string::npos ? std::string::npos : slash - at));
        if (slash == std::string::npos)
            break;
        at = slash + 1;
    }
    if (request.method == "GET" && parts == std::vector<std::string>{"me"})
        return answer(200, {{"email", email}});
    if (request.method == "GET" && parts == std::vector<std::string>{"posts"}) {
        Json posts = Json::array();
        for (const auto& [id, post] : m_state.posts) {
            Json entry = publicPost(id, post);
            entry["files"] = filesJson(post.files);
            posts.push_back(std::move(entry));
        }
        return answer(200, {{"posts", posts}});
    }
    if (request.method != "POST" || parts.size() < 3 || parts[0] != "posts")
        return answer(404, {{"error", "no such action"}});
    const Snowflake id = parseSnowflake(parts[1]).value_or(0);
    const auto post = m_state.posts.find(id);
    if (post == m_state.posts.end())
        return answer(404, {{"error", "no such post"}});
    const std::string name = std::format("{}#{}", post->second.repo, post->second.number);

    if (parts.size() == 3 && parts[2] == "update") {
        const std::string result = update(id, 0, std::nullopt);
        Log::info("staff", "{} updated {} ({}): {}", email, id, name, result);
        return answer(200, {{"result", result}});
    }
    if (parts.size() == 5 && parts[2] == "files" && parts[4] == "remove") {
        auto& files = post->second.files;
        const auto index = parseSnowflake(parts[3]); // 1-based
        if (!index || *index > files.size() || files[*index - 1].removed)
            return answer(404, {{"error", "no such file"}});
        KeptFile& file = files[*index - 1];
        std::error_code error;
        std::filesystem::remove(m_config.filesDir + "/" + file.path, error);
        file.removed = true;
        Log::info("staff", "{} took down {} of {} ({})", email, file.path, id, name);
        save();
        return answer(200, {{"result", std::format("{} is taken down.", file.name)}});
    }
    if (parts.size() == 3 && parts[2] == "unlink") {
        const bool removeFiles = request.query.find("files=remove") != std::string::npos;
        if (removeFiles) {
            std::error_code error;
            std::filesystem::remove_all(std::format("{}/{}", m_config.filesDir, id), error);
        }
        m_state.posts.erase(post);
        Log::info("staff", "{} unlinked {} from {}{}", email, id, name, removeFiles ? ", its files taken down" : "");
        save();
        return answer(200, {{"result", std::format("The post is no longer linked to {}.", name)}});
    }
    return answer(404, {{"error", "no such action"}});
}

} // namespace Glim
