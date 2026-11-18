// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Cards.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace Wisp {

using Json = nlohmann::json;

namespace {

/// How long a push's or a release's message is drawn again (its CI runs, the
/// files a release gets after it is published); its record goes later.
constexpr std::int64_t FeedFollow = 24 * 3600;
constexpr std::int64_t FeedForget = 7 * 24 * 3600;
/// Release ids kept for each repository, to know which were posted.
constexpr std::size_t KeptReleases = 50;
/// A comment as a card keeps it: its first words.
constexpr std::size_t KeptComment = 600;
/// Messages looked at by cleanUp() in each channel.
constexpr int CleanUpPages = 10;

std::string itemKey(const Item& item)
{
    return std::format("{}:{}#{}", item.pull ? "pr" : "issue", item.repo, item.number);
}

/// "pr:Repo#12" -> {"pr", "Repo", 12}; nothing for another kind of key.
struct ItemKey
{
    std::string kind;
    std::string repo;
    int number = 0;
};
std::optional<ItemKey> parseItemKey(const std::string& key)
{
    const auto colon = key.find(':');
    const auto hash = key.rfind('#');
    if (colon == std::string::npos || hash == std::string::npos || hash < colon)
        return std::nullopt;
    ItemKey out{key.substr(0, colon), key.substr(colon + 1, hash - colon - 1), std::atoi(key.c_str() + hash + 1)};
    if ((out.kind != "pr" && out.kind != "issue") || out.number <= 0)
        return std::nullopt;
    return out;
}

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Snowflake messageId(const DiscordResult& result)
{
    if (!result.body.is_object() || !result.body.contains("id") || !result.body["id"].is_string())
        return 0;
    return parseSnowflake(result.body["id"].get<std::string>()).value_or(0);
}

} // namespace

Cards::Cards(const Config& config, GitHub& github, Discord& discord, State& state, Now now)
    : m_config(config)
    , m_github(github)
    , m_discord(discord)
    , m_state(state)
    , m_now(now ? std::move(now) : Now(systemNow))
    , m_look{config.org, config.iconUrl, {}}
{
}

std::string Cards::hash(const Json& message)
{
    std::uint64_t value = 0xcbf29ce484222325ULL;
    for (const char c : message.dump()) {
        value ^= static_cast<unsigned char>(c);
        value *= 0x100000001b3ULL;
    }
    return std::format("{:016x}", value);
}

bool Cards::refreshRepositories()
{
    const auto all = m_github.repositories();
    if (!all)
        return false;
    std::vector<Repository> followed;
    for (const auto& repo : *all) {
        const bool listed = std::find(m_config.repos.begin(), m_config.repos.end(), repo.name) != m_config.repos.end();
        if (m_config.repos.empty() ? (!repo.archived && !repo.fork && !repo.isPrivate) : listed)
            followed.push_back(repo);
    }
    m_repositories = std::move(followed);
    return true;
}

void Cards::round()
{
    refreshRepositories();
    for (const auto& repo : m_repositories) {
        if (m_config.cardsPulls || m_config.cardsIssues)
            repoCards(repo);
        if (m_config.feed)
            repoFeed(repo);
    }
}

void Cards::repoCards(const Repository& repo)
{
    auto open = m_github.openItems(repo.name);
    if (!open)
        return;

    // The newest people's comments: each is the latest of its item when it is
    // newer than the one kept.
    if (auto comments = m_github.recentComments(repo.name)) {
        for (auto it = comments->rbegin(); it != comments->rend(); ++it) {
            const std::string key = std::format("{}#{}", repo.name, it->issue);
            const auto kept = m_state.latestComments.find(key);
            if (kept == m_state.latestComments.end() || it->createdAt >= kept->second.createdAt) {
                Comment comment = *it;
                comment.body = Embeds::shorten(comment.body, KeptComment);
                m_state.latestComments[key] = std::move(comment);
            }
        }
    }

    std::set<std::string> drawn;
    for (auto& item : *open) {
        const auto channel = item.pull ? m_config.cardsPulls : m_config.cardsIssues;
        if (!channel)
            continue;
        drawn.insert(itemKey(item));
        if (item.pull && !m_github.fillPull(item))
            continue;
        drawItem(item, *channel, 0);
    }

    // The cards of items that are open no more.
    std::vector<std::string> others;
    for (const auto& [key, card] : m_state.cards) {
        const auto parsed = parseItemKey(key);
        if (parsed && parsed->repo == repo.name && !drawn.contains(key))
            others.push_back(key);
    }
    const std::int64_t now = m_now();
    for (const auto& key : others) {
        const auto parsed = parseItemKey(key);
        const CardRecord card = m_state.cards[key];
        const auto channel = parsed->kind == "pr" ? m_config.cardsPulls : m_config.cardsIssues;
        if (!channel || (card.closedAt && now - card.closedAt >= m_config.closedKeep.count() * 3600)) {
            retire(key);
            continue;
        }
        bool gone = false;
        const auto item = m_github.item(repo.name, parsed->number, &gone);
        if (!item) {
            if (gone)
                retire(key);
            continue;
        }
        drawItem(*item, *channel, item->state == "open" ? 0 : (card.closedAt ? card.closedAt : now));
    }
}

void Cards::drawItem(const Item& item, Snowflake channel, std::int64_t closedAt)
{
    Embeds::CardExtras extras;
    if (item.pull && !item.headSha.empty()) {
        auto runs = m_github.runs(item.repo, item.headSha);
        if (!runs)
            return; // not whole: the next round
        extras.runs = std::move(*runs);
    }
    if (const auto it = m_state.latestComments.find(std::format("{}#{}", item.repo, item.number));
        it != m_state.latestComments.end())
        extras.latestComment = it->second;

    const std::string result = Embeds::runsResult(extras.runs);
    const std::string activity = std::format("{}|{}|{}|{}|{}", Embeds::itemState(item), item.headSha,
                                             extras.latestComment ? extras.latestComment->url : std::string(),
                                             result == "pending" ? std::string() : result, item.draft);
    const std::string key = itemKey(item);
    sync(key, channel, Embeds::itemCard(look(), item, extras), activity);
    if (const auto it = m_state.cards.find(key); it != m_state.cards.end())
        it->second.closedAt = closedAt;
}

std::optional<Embeds::Push> Cards::push(const Repository& repo, const std::string& before, const std::string& head)
{
    Embeds::Push out;
    out.repo = repo.name;
    out.branch = repo.defaultBranch;
    out.before = before;
    if (auto compared = m_github.compare(repo.name, before, head)) {
        out.commits = std::move(compared->first);
        out.total = compared->second;
    }
    if (!out.commits.empty() && out.commits.back().sha == head) {
        out.head = out.commits.back();
    } else {
        // A force push: GitHub compares nothing. The head alone is shown.
        auto commit = m_github.branchHead(repo.name, repo.defaultBranch);
        if (!commit || commit->sha != head)
            return std::nullopt;
        out.head = *commit;
        out.commits.clear();
        out.total = 1;
    }
    auto runs = m_github.runs(repo.name, head);
    if (!runs)
        return std::nullopt;
    out.runs = std::move(*runs);
    return out;
}

void Cards::repoFeed(const Repository& repo)
{
    const Snowflake channel = *m_config.feed;
    const bool fresh = !m_state.repos.contains(repo.name);
    const std::int64_t now = m_now();
    const std::string prefix = std::format("push:{}:", repo.name);

    if (const auto head = m_github.branchHead(repo.name, repo.defaultBranch)) {
        RepoRecord& record = m_state.repos[repo.name];
        if (record.head.empty()) {
            record.head = head->sha; // the first head seen is no push
        } else if (record.head != head->sha) {
            const std::string key = std::format("{}{}..{}", prefix, record.head, head->sha);
            if (const auto drawn = push(repo, record.head, head->sha)) {
                sync(key, channel, Embeds::pushCard(look(), *drawn), "");
                record.head = head->sha;
            }
        }
    }

    // Pushes of the last day are drawn again until their runs end.
    std::vector<std::string> pushes;
    for (const auto& [key, card] : m_state.cards) {
        if (key.starts_with(prefix))
            pushes.push_back(key);
    }
    for (const auto& key : pushes) {
        CardRecord& card = m_state.cards[key];
        if (now - card.postedAt >= FeedForget) {
            m_state.cards.erase(key); // the message stays; it is no more followed
            continue;
        }
        if (card.closedAt || now - card.postedAt >= FeedFollow)
            continue;
        const std::string range = key.substr(prefix.size());
        const auto dots = range.find("..");
        if (dots == std::string::npos)
            continue;
        const auto drawn = push(repo, range.substr(0, dots), range.substr(dots + 2));
        if (!drawn)
            continue;
        sync(key, channel, Embeds::pushCard(look(), *drawn), "");
        const std::string result = Embeds::runsResult(drawn->runs);
        if (result == "success" || result == "failure")
            m_state.cards[key].closedAt = now; // settled
    }

    const auto releases = m_github.releases(repo.name);
    if (!releases)
        return;
    RepoRecord& record = m_state.repos[repo.name];
    for (auto it = releases->rbegin(); it != releases->rend(); ++it) {
        const Release& release = *it;
        if (release.draft || !release.id)
            continue;
        const std::string key = std::format("release:{}:{}", repo.name, release.id);
        const bool known =
            std::find(record.releases.begin(), record.releases.end(), release.id) != record.releases.end();
        if (!known) {
            record.releases.push_back(release.id);
            if (record.releases.size() > KeptReleases)
                record.releases.erase(record.releases.begin());
            if (fresh)
                continue; // the releases before Wisp came are no news
            sync(key, channel, Embeds::releaseCard(look(), repo.name, release), "");
        } else if (const auto card = m_state.cards.find(key); card != m_state.cards.end()) {
            if (now - card->second.postedAt >= FeedForget)
                m_state.cards.erase(card);
            else if (now - card->second.postedAt < FeedFollow)
                sync(key, channel, Embeds::releaseCard(look(), repo.name, release), "");
        }
    }
}

void Cards::sync(const std::string& key, Snowflake channel, const Json& message, const std::string& activity)
{
    const std::string drawn = hash(message);
    const auto existing = m_state.cards.find(key);
    const bool have = existing != m_state.cards.end() && existing->second.message;

    const auto post = [&]() -> bool {
        const DiscordResult result = m_discord.send(channel, message);
        const Snowflake id = messageId(result);
        if (!result.ok() || !id) {
            Log::info("cards", "{}: could not post ({} {})", key, result.status,
                      result.status ? result.body.dump() : result.error);
            return false;
        }
        const CardRecord old = have ? existing->second : CardRecord{};
        CardRecord& card = m_state.cards[key];
        card = {channel, id, drawn, activity, m_now(), old.closedAt};
        if (have)
            m_discord.remove(old.channel, old.message);
        return true;
    };

    if (!have || existing->second.channel != channel) {
        post();
        return;
    }
    CardRecord& card = existing->second;
    if (!activity.empty() && card.activity != activity) {
        post();
        return;
    }
    if (card.drawn != drawn) {
        const DiscordResult result = m_discord.edit(card.channel, card.message, message);
        if (result.status == 404) {
            post(); // someone deleted it
            return;
        }
        if (result.ok())
            card.drawn = drawn;
        else
            Log::info("cards", "{}: could not edit ({})", key, result.status);
    }
    card.activity = activity;
}

void Cards::retire(const std::string& key)
{
    const auto it = m_state.cards.find(key);
    if (it == m_state.cards.end())
        return;
    if (it->second.message) {
        const DiscordResult result = m_discord.remove(it->second.channel, it->second.message);
        if (!result.ok() && result.status != 404) {
            Log::info("cards", "{}: could not delete ({})", key, result.status);
            return; // again next round
        }
    }
    m_state.cards.erase(it);
}

void Cards::cleanUp(Snowflake botUser)
{
    std::set<Snowflake> kept;
    for (const auto& [key, card] : m_state.cards)
        kept.insert(card.message);
    for (const auto& channel : {m_config.cardsPulls, m_config.cardsIssues}) {
        if (!channel)
            continue;
        Snowflake before = 0;
        int deleted = 0;
        for (int page = 0; page < CleanUpPages; ++page) {
            const DiscordResult result = m_discord.messages(*channel, before);
            if (!result.ok() || !result.body.is_array() || result.body.empty())
                break;
            for (const auto& message : result.body) {
                const Snowflake id = parseSnowflake(message.value("id", "")).value_or(0);
                if (id)
                    before = before ? std::min(before, id) : id;
                const Snowflake author =
                    message.contains("author") ? parseSnowflake(message["author"].value("id", "")).value_or(0) : 0;
                if (id && author == botUser && !kept.contains(id) && m_discord.remove(*channel, id).ok())
                    ++deleted;
            }
            if (result.body.size() < 100)
                break;
        }
        if (deleted)
            Log::info("cards", "deleted {} messages of mine no card keeps in channel {}", deleted, *channel);
    }
}

} // namespace Wisp
