// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Http.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Bots {

struct Label
{
    std::string name;
    std::string color; ///< six hex digits
    bool operator==(const Label&) const = default;
};

/// An issue or a pull request, as the cards and the replies draw it.
struct Item
{
    std::string repo;
    int number = 0;
    bool pull = false;
    std::string title;
    std::string url;
    std::string state; ///< open, closed
    std::string stateReason; ///< completed, not_planned, reopened (issues)
    bool merged = false;
    bool draft = false;
    std::string author;
    std::vector<Label> labels;
    std::string createdAt;
    std::string updatedAt;
    std::string closedAt;
    int comments = 0;
    // Pull requests only (GET /pulls/N):
    std::string headRef;
    std::string headLabel; ///< owner:branch
    std::string baseRef;
    std::string headSha;
    int commits = 0;
    int additions = 0;
    int deletions = 0;
    int changedFiles = 0;
    bool operator==(const Item&) const = default;
};

/// A run of a workflow of GitHub Actions: Arch, Lint, Nix...
struct WorkflowRun
{
    long long id = 0;
    std::string name;
    std::string status; ///< queued, in_progress, completed...
    std::string conclusion; ///< success, failure, cancelled, skipped... (when completed)
    std::string url;
    std::string headSha;
    std::string createdAt;
    bool operator==(const WorkflowRun&) const = default;
};

struct Comment
{
    int issue = 0;
    std::string author;
    std::string body;
    std::string url;
    std::string createdAt;
    bool operator==(const Comment&) const = default;
};

struct Commit
{
    std::string sha;
    std::string message; ///< its first line
    std::string author;
    std::string url;
    bool operator==(const Commit&) const = default;
};

struct Asset
{
    std::string name;
    long long size = 0;
    std::string url;
    bool operator==(const Asset&) const = default;
};

struct Release
{
    long long id = 0;
    std::string tag;
    std::string name;
    std::string url;
    std::string body;
    std::string author;
    std::string publishedAt;
    bool prerelease = false;
    bool draft = false;
    std::vector<Asset> assets;
    bool operator==(const Release&) const = default;
};

struct Repository
{
    std::string name;
    std::string defaultBranch;
    bool archived = false;
    bool fork = false;
    bool isPrivate = false;
};

/// GitHub's REST API for one organization, with each answer kept with its
/// ETag: a question asked again costs nothing of the hourly limit while its
/// answer is unchanged (304).
class GitHub
{
public:
    /// Where each request's token comes from: a GitHubApp's, say. Empty: none.
    using TokenSource = std::function<std::string()>;

    GitHub(std::string org, std::string token, std::string userAgent, HttpTransport transport);
    GitHub(std::string org, TokenSource token, std::string userAgent, HttpTransport transport);

    const std::string& org() const
    {
        return m_org;
    }

    /// A GET of @p path (from /repos..., /orgs...). Nothing on failure, said in
    /// the log once for each kind of failure; a 404 gives nothing quietly.
    std::optional<nlohmann::json> get(const std::string& path, long* status = nullptr);

    /// A POST or PATCH of @p body to @p path; the answer when GitHub took it
    /// (2xx), nothing otherwise (said in the log).
    std::optional<nlohmann::json> send(const std::string& method, const std::string& path, const nlohmann::json& body,
                                       long* status = nullptr);
    /// A new issue in @p repo: its number and address.
    std::optional<Item> createIssue(const std::string& repo, const std::string& title, const std::string& body,
                                    const std::vector<std::string>& labels = {});
    /// A comment on issue @p number: its address.
    std::optional<std::string> comment(const std::string& repo, int number, const std::string& body);

    std::optional<std::vector<Repository>> repositories();
    /// The organization's avatar, as a PNG address of GitHub's.
    std::optional<std::string> avatarUrl();
    /// One issue or pull request, with a pull request's own fields. @p gone
    /// is set when GitHub has it no more (deleted, or moved to another repository).
    std::optional<Item> item(const std::string& repo, int number, bool* gone = nullptr);
    /// The open issues and pull requests (pull requests without their own fields).
    std::optional<std::vector<Item>> openItems(const std::string& repo);
    /// A pull request's own fields, over @p item.
    bool fillPull(Item& item);
    /// The newest run of each workflow for commit @p sha.
    std::optional<std::vector<WorkflowRun>> runs(const std::string& repo, const std::string& sha);
    /// The newest comments of the repository, newest first (people's only).
    std::optional<std::vector<Comment>> recentComments(const std::string& repo);
    /// The head commit of @p branch.
    std::optional<Commit> branchHead(const std::string& repo, const std::string& branch);
    /// The commits from @p base (excluded) to @p head, oldest first, and their count.
    std::optional<std::pair<std::vector<Commit>, int>> compare(const std::string& repo, const std::string& base,
                                                               const std::string& head);
    std::optional<std::vector<Release>> releases(const std::string& repo);

    /// Requests left of the hour, as GitHub last said (-1: not known).
    long remaining() const
    {
        return m_remaining;
    }

    // Parsers, public for the tests.
    static Item parseItem(const std::string& repo, const nlohmann::json& issue);
    static void parsePull(Item& item, const nlohmann::json& pull);
    static std::vector<WorkflowRun> parseRuns(const nlohmann::json& runs);
    static Release parseRelease(const nlohmann::json& release);
    static Commit parseCommit(const nlohmann::json& commit);
    /// A person's comment, or nothing for a bot's.
    static std::optional<Comment> parseComment(const nlohmann::json& comment);

private:
    struct Cached
    {
        std::string etag;
        nlohmann::json body;
    };
    std::string m_org;
    std::vector<std::string> headers();

    TokenSource m_token;
    std::string m_userAgent;
    HttpTransport m_transport;
    std::map<std::string, Cached> m_cache;
    std::string m_lastError;
    long m_remaining = -1;
};

} // namespace Bots
