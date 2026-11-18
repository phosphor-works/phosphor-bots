// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Embeds.h"

#include <format>

namespace Wisp::Embeds {

using Json = nlohmann::json;

namespace {

using namespace Brand;

constexpr std::size_t TitleLimit = 256;
constexpr std::size_t DescriptionLimit = 4096;
constexpr std::size_t CommentLimit = 300;
constexpr std::size_t ReleaseNotesLimit = 700;
constexpr std::size_t MaxPushCommits = 10;
constexpr std::size_t MaxAssets = 12;

std::string runIcon(const WorkflowRun& run)
{
    if (run.status != "completed")
        return "⏳";
    if (run.conclusion == "success")
        return "✅";
    if (run.conclusion == "failure" || run.conclusion == "timed_out" || run.conclusion == "startup_failure")
        return "❌";
    return "⚪"; // cancelled, skipped, neutral, stale, action_required
}

std::string labelsLine(const std::vector<Label>& labels)
{
    std::string out;
    for (const auto& label : labels) {
        if (!out.empty())
            out += ", ";
        out += "`" + std::string(shorten(label.name, 50)) + "`";
    }
    return out;
}

std::string size(long long bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return std::format("{:.1f} GiB", double(bytes) / (1024.0 * 1024 * 1024));
    if (bytes >= 1024LL * 1024)
        return std::format("{:.1f} MiB", double(bytes) / (1024.0 * 1024));
    if (bytes >= 1024)
        return std::format("{:.0f} KiB", double(bytes) / 1024.0);
    return std::format("{} B", bytes);
}

/// A comment's text as a quote: its first lines, no images or HTML comments.
std::string quote(const std::string& body)
{
    std::string text;
    bool inComment = false;
    for (std::size_t i = 0; i < body.size();) {
        if (!inComment && body.compare(i, 4, "<!--") == 0) {
            inComment = true;
            i += 4;
        } else if (inComment && body.compare(i, 3, "-->") == 0) {
            inComment = false;
            i += 3;
        } else {
            if (!inComment && body[i] != '\r')
                text += body[i];
            ++i;
        }
    }
    while (!text.empty() && (text.front() == '\n' || text.front() == ' '))
        text.erase(0, 1);
    std::string out;
    std::size_t start = 0;
    int lines = 0;
    const std::string cut = shorten(text, CommentLimit);
    while (start <= cut.size() && lines < 4) {
        const auto end = cut.find('\n', start);
        const std::string line = cut.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) {
            out += "> " + escape(line) + "\n";
            ++lines;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

/// An embed in Wisp's look: the repository above the title, Wisp below.
Json embed(const Look& look, const std::string& repo, std::string title, std::string url, int color,
           std::string description)
{
    Json out = {{"title", shorten(title, TitleLimit)}, {"color", color}};
    out["author"] = {{"name", std::format("{} / {}", look.org, repo)},
                     {"url", std::format("https://github.com/{}/{}", look.org, repo)}};
    if (!look.orgIconUrl.empty())
        out["author"]["icon_url"] = look.orgIconUrl;
    Json footer = {{"text", "Wisp · " + look.org}};
    if (!look.iconUrl.empty())
        footer["icon_url"] = look.iconUrl;
    out["footer"] = footer;
    if (!url.empty())
        out["url"] = url;
    if (!description.empty())
        out["description"] = shorten(description, DescriptionLimit);
    return out;
}

} // namespace

std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (std::string_view("\\*_~`|>[]()#-").find(c) != std::string_view::npos)
            out += '\\';
        out += c;
    }
    return out;
}

std::string shorten(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string(text);
    std::size_t cut = limit >= 3 ? limit - 3 : 0;
    // Not inside a UTF-8 sequence.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return std::string(text.substr(0, cut)) + "…";
}

int itemColor(const Item& item)
{
    if (item.state == "open")
        return item.draft ? Slate : Cyan;
    if (item.pull)
        return item.merged ? Purple : Rose;
    return item.stateReason == "not_planned" ? Slate : Purple;
}

std::string itemState(const Item& item)
{
    const char* kind = item.pull ? "pull request" : "issue";
    if (item.state == "open")
        return std::format("{}{}", item.draft ? "draft " : "open ", kind);
    if (item.pull)
        return std::format("{} {}", item.merged ? "merged" : "closed", kind);
    return std::format("{} {}", item.stateReason == "not_planned" ? "closed (not planned)" : "closed", kind);
}

std::string runsLine(const std::vector<WorkflowRun>& runs)
{
    std::string out;
    for (const auto& run : runs) {
        if (!out.empty())
            out += " · ";
        out += std::format("{} [{}]({})", runIcon(run), escape(run.name), run.url);
    }
    return out;
}

std::string runsResult(const std::vector<WorkflowRun>& runs)
{
    if (runs.empty())
        return {};
    bool pending = false;
    for (const auto& run : runs) {
        const auto icon = runIcon(run);
        if (icon == "❌")
            return "failure";
        if (icon == "⏳")
            pending = true;
    }
    return pending ? "pending" : "success";
}

Json reply(const Look& look, const std::vector<Item>& items)
{
    Json embeds = Json::array();
    for (const auto& item : items) {
        std::string description = std::format("{} by **{}**", itemState(item), escape(item.author));
        if (item.pull && !item.baseRef.empty())
            description += std::format(" · `{}` → `{}`", shorten(item.headLabel, 80), item.baseRef);
        if (!item.labels.empty())
            description += "\n" + labelsLine(item.labels);
        embeds.push_back(embed(look, item.repo, std::format("#{} · {}", item.number, item.title), item.url,
                               itemColor(item), description));
    }
    return {{"content", ""}, {"embeds", embeds}};
}

Json itemCard(const Look& look, const Item& item, const CardExtras& extras)
{
    std::string description = std::format("{} by **{}**", itemState(item), escape(item.author));
    if (item.pull) {
        if (!item.baseRef.empty())
            description += std::format("\n`{}` → `{}`", shorten(item.headLabel, 80), item.baseRef);
        if (item.commits)
            description +=
                std::format("\n{} commit{} · +{} −{} in {} file{}", item.commits, item.commits == 1 ? "" : "s",
                            item.additions, item.deletions, item.changedFiles, item.changedFiles == 1 ? "" : "s");
    }
    if (!item.labels.empty())
        description += "\n" + labelsLine(item.labels);
    if (!extras.runs.empty())
        description += "\n" + runsLine(extras.runs);
    if (extras.latestComment) {
        const auto& comment = *extras.latestComment;
        description += std::format("\n\n[Latest comment]({}) by **{}**:\n{}", comment.url, escape(comment.author),
                                   quote(comment.body));
    }
    Json e = embed(look, item.repo, std::format("#{} · {}", item.number, item.title), item.url, itemColor(item),
                   description);
    if (!item.updatedAt.empty())
        e["timestamp"] = item.updatedAt;
    return {{"content", ""}, {"embeds", Json::array({e})}};
}

Json pushCard(const Look& look, const Push& push)
{
    std::string description;
    const std::size_t first = push.commits.size() > MaxPushCommits ? push.commits.size() - MaxPushCommits : 0;
    for (std::size_t i = push.commits.size(); i-- > first;) {
        const auto& commit = push.commits[i];
        description += std::format("[`{}`]({}) {} · {}\n", commit.sha.substr(0, 7), commit.url,
                                   escape(shorten(commit.message, 100)), escape(commit.author));
    }
    if (push.commits.empty())
        description += std::format("[`{}`]({}) {} · {}\n", push.head.sha.substr(0, 7), push.head.url,
                                   escape(shorten(push.head.message, 100)), escape(push.head.author));
    const int shown = int(std::min(push.commits.size(), MaxPushCommits));
    if (push.total > shown && shown > 0)
        description += std::format("and {} more\n", push.total - shown);
    if (!push.runs.empty())
        description += "\n" + runsLine(push.runs);

    const int count = std::max(push.total, 1);
    const std::string result = runsResult(push.runs);
    const int color = result == "failure" ? Rose : result == "pending" ? Blue : result == "success" ? Cyan : Slate;
    const std::string url = push.before.empty()
        ? push.head.url
        : std::format("https://github.com/{}/{}/compare/{}...{}", look.org, push.repo, push.before.substr(0, 12),
                      push.head.sha.substr(0, 12));
    Json e = embed(look, push.repo, std::format("{} commit{} to {}", count, count == 1 ? "" : "s", push.branch), url,
                   color, description);
    return {{"content", ""}, {"embeds", Json::array({e})}};
}

Json releaseCard(const Look& look, const std::string& repo, const Release& release)
{
    std::string description;
    if (release.prerelease)
        description += "**Pre-release**\n";
    if (!release.body.empty())
        description += quote(shorten(release.body, ReleaseNotesLimit)) + "\n";
    std::size_t shown = 0;
    for (const auto& asset : release.assets) {
        if (shown++ == MaxAssets) {
            description += std::format("and {} more files\n", release.assets.size() - MaxAssets);
            break;
        }
        description += std::format("[{}]({}) ({})\n", escape(asset.name), asset.url, size(asset.size));
    }
    const std::string title = release.name.empty() ? release.tag : release.name;
    Json e = embed(look, repo, std::format("{} {}", repo, title), release.url, release.prerelease ? Purple : Blue,
                   description);
    if (!release.publishedAt.empty())
        e["timestamp"] = release.publishedAt;
    return {{"content", ""}, {"embeds", Json::array({e})}};
}

} // namespace Wisp::Embeds
