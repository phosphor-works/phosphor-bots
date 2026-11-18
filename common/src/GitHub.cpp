// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GitHub.h"

#include "Log.h"

#include <algorithm>
#include <cstdlib>
#include <format>

namespace Bots {

using Json = nlohmann::json;

namespace {

constexpr std::string_view Api = "https://api.github.com";
/// Answers kept for their ETags; past this the oldest kind is dropped whole.
constexpr std::size_t MaxCached = 2000;

std::string text(const Json& object, const char* key)
{
    if (!object.is_object())
        return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

long long integer(const Json& object, const char* key)
{
    if (!object.is_object())
        return 0;
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<long long>() : 0;
}

bool flag(const Json& object, const char* key)
{
    if (!object.is_object())
        return false;
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

std::string login(const Json& object, const char* key)
{
    return object.is_object() && object.contains(key) ? text(object[key], "login") : std::string();
}

bool isBot(const Json& user)
{
    return text(user, "type") == "Bot" || text(user, "login").ends_with("[bot]");
}

std::string firstLine(const std::string& message)
{
    return message.substr(0, message.find('\n'));
}

} // namespace

GitHub::GitHub(std::string org, std::string token, std::string userAgent, HttpTransport transport)
    : m_org(std::move(org))
    , m_token(std::move(token))
    , m_userAgent(std::move(userAgent))
    , m_transport(std::move(transport))
{
}

std::optional<Json> GitHub::get(const std::string& path, long* status)
{
    HttpRequest request;
    request.url = std::string(Api) + path;
    request.headers = {
        "Accept: application/vnd.github+json",
        "X-GitHub-Api-Version: 2022-11-28",
        "User-Agent: " + m_userAgent,
    };
    if (!m_token.empty())
        request.headers.push_back("Authorization: Bearer " + m_token);
    const auto cached = m_cache.find(path);
    if (cached != m_cache.end() && !cached->second.etag.empty())
        request.headers.push_back("If-None-Match: " + cached->second.etag);

    const HttpResponse response = m_transport(request);
    if (status)
        *status = response.status;
    if (const auto left = response.header("x-ratelimit-remaining"); !left.empty())
        m_remaining = std::strtol(left.c_str(), nullptr, 10);

    if (response.status == 304 && cached != m_cache.end()) {
        m_lastError.clear();
        return cached->second.body;
    }
    if (response.status == 200) {
        Json body = Json::parse(response.body, nullptr, false);
        if (body.is_discarded()) {
            Log::info("github", "GET {}: an answer that is no JSON", path);
            return std::nullopt;
        }
        if (const auto etag = response.header("etag"); !etag.empty()) {
            if (m_cache.size() >= MaxCached)
                m_cache.clear();
            m_cache[path] = {etag, body};
        }
        m_lastError.clear();
        return body;
    }
    if (response.status == 404 || response.status == 410 || response.status == 301)
        return std::nullopt;
    // Say a failure once, not every round it lasts.
    const std::string error = response.status == 0
        ? response.error
        : std::format("{} {}", response.status, text(Json::parse(response.body, nullptr, false), "message"));
    if (error != m_lastError) {
        Log::info("github", "GET {}: {}{}", path, error,
                  (response.status == 403 || response.status == 429) && m_remaining == 0 ? " (the hourly limit)" : "");
        m_lastError = error;
    }
    return std::nullopt;
}

Item GitHub::parseItem(const std::string& repo, const Json& issue)
{
    Item item;
    item.repo = repo;
    item.number = int(integer(issue, "number"));
    item.pull = issue.contains("pull_request") || issue.contains("head");
    item.title = text(issue, "title");
    item.url = text(issue, "html_url");
    item.state = text(issue, "state");
    item.stateReason = text(issue, "state_reason");
    item.draft = flag(issue, "draft");
    item.author = login(issue, "user");
    item.createdAt = text(issue, "created_at");
    item.updatedAt = text(issue, "updated_at");
    item.closedAt = text(issue, "closed_at");
    item.comments = int(integer(issue, "comments"));
    if (issue.contains("pull_request") && issue["pull_request"].is_object())
        item.merged = !text(issue["pull_request"], "merged_at").empty();
    if (issue.contains("labels") && issue["labels"].is_array()) {
        for (const auto& label : issue["labels"])
            item.labels.push_back({text(label, "name"), text(label, "color")});
    }
    return item;
}

void GitHub::parsePull(Item& item, const Json& pull)
{
    item.pull = true;
    item.merged = flag(pull, "merged") || !text(pull, "merged_at").empty();
    item.draft = flag(pull, "draft");
    if (pull.contains("head")) {
        item.headRef = text(pull["head"], "ref");
        item.headLabel = text(pull["head"], "label");
        item.headSha = text(pull["head"], "sha");
    }
    if (pull.contains("base"))
        item.baseRef = text(pull["base"], "ref");
    item.commits = int(integer(pull, "commits"));
    item.additions = int(integer(pull, "additions"));
    item.deletions = int(integer(pull, "deletions"));
    item.changedFiles = int(integer(pull, "changed_files"));
}

std::vector<WorkflowRun> GitHub::parseRuns(const Json& runs)
{
    // The list is newest first: keep the first run of each workflow.
    std::vector<WorkflowRun> out;
    if (!runs.is_object() || !runs.contains("workflow_runs") || !runs["workflow_runs"].is_array())
        return out;
    for (const auto& run : runs["workflow_runs"]) {
        WorkflowRun parsed{integer(run, "id"),      text(run, "name"),     text(run, "status"),
                           text(run, "conclusion"), text(run, "html_url"), text(run, "head_sha"),
                           text(run, "created_at")};
        const bool seen = std::any_of(out.begin(), out.end(), [&](const WorkflowRun& r) {
            return r.name == parsed.name;
        });
        if (!seen)
            out.push_back(std::move(parsed));
    }
    std::sort(out.begin(), out.end(), [](const WorkflowRun& a, const WorkflowRun& b) {
        return a.name < b.name;
    });
    return out;
}

Release GitHub::parseRelease(const Json& release)
{
    Release out;
    out.id = integer(release, "id");
    out.tag = text(release, "tag_name");
    out.name = text(release, "name");
    out.url = text(release, "html_url");
    out.body = text(release, "body");
    out.author = login(release, "author");
    out.publishedAt = text(release, "published_at");
    out.prerelease = flag(release, "prerelease");
    out.draft = flag(release, "draft");
    if (release.contains("assets") && release["assets"].is_array()) {
        for (const auto& asset : release["assets"])
            out.assets.push_back({text(asset, "name"), integer(asset, "size"), text(asset, "browser_download_url")});
    }
    return out;
}

Commit GitHub::parseCommit(const Json& commit)
{
    Commit out;
    out.sha = text(commit, "sha");
    out.url = text(commit, "html_url");
    if (commit.contains("commit")) {
        out.message = firstLine(text(commit["commit"], "message"));
        if (commit["commit"].contains("author"))
            out.author = text(commit["commit"]["author"], "name");
    }
    if (const auto user = login(commit, "author"); !user.empty())
        out.author = user;
    return out;
}

std::optional<Comment> GitHub::parseComment(const Json& comment)
{
    if (!comment.is_object() || (comment.contains("user") && isBot(comment["user"])))
        return std::nullopt;
    Comment out;
    const std::string issueUrl = text(comment, "issue_url");
    const auto slash = issueUrl.rfind('/');
    if (slash != std::string::npos)
        out.issue = std::atoi(issueUrl.c_str() + slash + 1);
    out.author = login(comment, "user");
    out.body = text(comment, "body");
    out.url = text(comment, "html_url");
    out.createdAt = text(comment, "created_at");
    return out;
}

std::optional<std::vector<Repository>> GitHub::repositories()
{
    std::vector<Repository> out;
    for (int page = 1; page <= 10; ++page) {
        const auto body = get(std::format("/orgs/{}/repos?type=public&per_page=100&page={}", m_org, page));
        if (!body || !body->is_array())
            return page == 1 ? std::nullopt : std::optional(out);
        for (const auto& repo : *body) {
            out.push_back({text(repo, "name"), text(repo, "default_branch"), flag(repo, "archived"), flag(repo, "fork"),
                           flag(repo, "private")});
        }
        if (body->size() < 100)
            break;
    }
    return out;
}

std::optional<std::string> GitHub::avatarUrl()
{
    const auto body = get(std::format("/orgs/{}", m_org));
    if (!body)
        return std::nullopt;
    std::string url = text(*body, "avatar_url");
    if (url.empty())
        return std::nullopt;
    // A size Discord shows sharp beside a name.
    url += url.find('?') == std::string::npos ? "?s=64" : "&s=64";
    return url;
}

std::optional<Item> GitHub::item(const std::string& repo, int number, bool* gone)
{
    long status = 0;
    const auto issue = get(std::format("/repos/{}/{}/issues/{}", m_org, repo, number), &status);
    if (gone)
        *gone = status == 404 || status == 410 || status == 301;
    if (!issue || !issue->is_object())
        return std::nullopt;
    Item out = parseItem(repo, *issue);
    // An issue moved to another repository answers with that one's.
    if (out.number != number) {
        if (gone)
            *gone = true;
        return std::nullopt;
    }
    if (out.pull && !fillPull(out))
        return std::nullopt;
    return out;
}

bool GitHub::fillPull(Item& item)
{
    const auto pull = get(std::format("/repos/{}/{}/pulls/{}", m_org, item.repo, item.number));
    if (!pull || !pull->is_object())
        return false;
    parsePull(item, *pull);
    return true;
}

std::optional<std::vector<Item>> GitHub::openItems(const std::string& repo)
{
    std::vector<Item> out;
    for (int page = 1; page <= 10; ++page) {
        const auto body = get(std::format("/repos/{}/{}/issues?state=open&per_page=100&page={}", m_org, repo, page));
        if (!body || !body->is_array())
            return std::nullopt;
        for (const auto& issue : *body)
            out.push_back(parseItem(repo, issue));
        if (body->size() < 100)
            break;
    }
    return out;
}

std::optional<std::vector<WorkflowRun>> GitHub::runs(const std::string& repo, const std::string& sha)
{
    const auto body = get(std::format("/repos/{}/{}/actions/runs?head_sha={}&per_page=50", m_org, repo, sha));
    if (!body)
        return std::nullopt;
    return parseRuns(*body);
}

std::optional<std::vector<Comment>> GitHub::recentComments(const std::string& repo)
{
    const auto body =
        get(std::format("/repos/{}/{}/issues/comments?sort=created&direction=desc&per_page=50", m_org, repo));
    if (!body || !body->is_array())
        return std::nullopt;
    std::vector<Comment> out;
    for (const auto& comment : *body) {
        if (auto parsed = parseComment(comment))
            out.push_back(std::move(*parsed));
    }
    return out;
}

std::optional<Commit> GitHub::branchHead(const std::string& repo, const std::string& branch)
{
    const auto body = get(std::format("/repos/{}/{}/branches/{}", m_org, repo, branch));
    if (!body || !body->is_object() || !body->contains("commit"))
        return std::nullopt;
    return parseCommit((*body)["commit"]);
}

std::optional<std::pair<std::vector<Commit>, int>> GitHub::compare(const std::string& repo, const std::string& base,
                                                                   const std::string& head)
{
    const auto body = get(std::format("/repos/{}/{}/compare/{}...{}?per_page=100", m_org, repo, base, head));
    if (!body || !body->is_object())
        return std::nullopt;
    std::vector<Commit> commits;
    if (body->contains("commits") && (*body)["commits"].is_array()) {
        for (const auto& commit : (*body)["commits"])
            commits.push_back(parseCommit(commit));
    }
    return std::pair(std::move(commits), int(integer(*body, "total_commits")));
}

std::optional<std::vector<Release>> GitHub::releases(const std::string& repo)
{
    const auto body = get(std::format("/repos/{}/{}/releases?per_page=10", m_org, repo));
    if (!body || !body->is_array())
        return std::nullopt;
    std::vector<Release> out;
    for (const auto& release : *body)
        out.push_back(parseRelease(release));
    return out;
}

} // namespace Bots
