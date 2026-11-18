// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Cards.h"

#include <gtest/gtest.h>

using namespace Wisp;

namespace {

constexpr Snowflake Pulls = 100;
constexpr Snowflake Issues = 200;
constexpr Snowflake Feed = 300;

/// A GitHub of one repository, and a Discord that keeps the messages.
struct World
{
    // GitHub
    Json issues = Json::array(); ///< the open ones
    std::map<int, Json> items; ///< every one, by number
    std::map<int, Json> pulls;
    Json runs = {{"workflow_runs", Json::array()}};
    Json comments = Json::array();
    std::string head = "a1";
    Json releases = Json::array();
    // Discord
    std::map<Snowflake, std::pair<Snowflake, Json>> messages; ///< id -> channel, body
    std::vector<std::string> calls;
    Snowflake nextId = 1000;
    std::int64_t now = 1'000'000;

    Config config;
    State state;
    GitHub github{"phosphor-works", "t", "test", [this](const HttpRequest& r) {
                      return gitHub(r);
                  }};
    Discord discord{"t", "test",
                    [this](const HttpRequest& r) {
                        return discordCall(r);
                    },
                    [](std::chrono::milliseconds) { }};
    Cards cards{config, github, discord, state, [this] {
                    return now;
                }};

    World()
    {
        config.token = "t";
        config.githubToken = "g";
        config.cardsPulls = Pulls;
        config.cardsIssues = Issues;
        config.feed = Feed;
    }

    static HttpResponse json(const Json& body)
    {
        return {200, {}, body.dump(), {}};
    }

    HttpResponse gitHub(const HttpRequest& request)
    {
        const std::string path = request.url.substr(std::string("https://api.github.com").size());
        const std::string repo = "/repos/phosphor-works/Afterglow";
        if (path.starts_with("/orgs/phosphor-works/repos"))
            return json(Json::array({{{"name", "Afterglow"}, {"default_branch", "main"}}}));
        if (path.starts_with(repo + "/issues?state=open"))
            return json(issues);
        if (path.starts_with(repo + "/issues/comments"))
            return json(comments);
        if (path.starts_with(repo + "/issues/")) {
            const int n = std::stoi(path.substr(repo.size() + 8));
            return items.contains(n) ? json(items[n]) : HttpResponse{404, {}, "{}", {}};
        }
        if (path.starts_with(repo + "/pulls/"))
            return json(pulls[std::stoi(path.substr(repo.size() + 7))]);
        if (path.starts_with(repo + "/actions/runs"))
            return json(runs);
        if (path.starts_with(repo + "/branches/main"))
            return json({{"commit", {{"sha", head}, {"commit", {{"message", "Head " + head}}}}}});
        if (path.starts_with(repo + "/compare/")) {
            return json({{"total_commits", 1},
                         {"commits", Json::array({{{"sha", head}, {"commit", {{"message", "Head " + head}}}}})}});
        }
        if (path.starts_with(repo + "/releases"))
            return json(releases);
        return {404, {}, "{}", {}};
    }

    HttpResponse discordCall(const HttpRequest& request)
    {
        const std::string path = request.url.substr(std::string("https://discord.com/api/v10/channels/").size());
        const Snowflake channel = std::stoull(path);
        calls.push_back(request.method);
        if (request.method == "POST") {
            const Snowflake id = nextId++;
            messages[id] = {channel, Json::parse(request.body)};
            return json({{"id", std::to_string(id)}});
        }
        const Snowflake id = std::stoull(path.substr(path.rfind('/') + 1));
        if (!messages.contains(id))
            return {404, {}, "{}", {}};
        if (request.method == "PATCH") {
            messages[id].second = Json::parse(request.body);
            return json({{"id", std::to_string(id)}});
        }
        messages.erase(id);
        return {204, {}, "", {}};
    }

    void openPull(int n, const std::string& sha)
    {
        Json issue = {{"number", n},
                      {"title", "PR " + std::to_string(n)},
                      {"state", "open"},
                      {"user", {{"login", "a"}}},
                      {"pull_request", Json::object()},
                      {"html_url", "u"}};
        items[n] = issue;
        pulls[n] = {{"head", {{"sha", sha}, {"ref", "b"}, {"label", "o:b"}}},
                    {"base", {{"ref", "main"}}},
                    {"commits", 1}};
        issues = Json::array();
        for (const auto& [number, item] : items) {
            if (item["state"] == "open")
                issues.push_back(item);
        }
    }

    void close(int n)
    {
        items[n]["state"] = "closed";
        items[n]["pull_request"] = {{"merged_at", "2026-11-18T00:00:00Z"}};
        pulls[n]["merged"] = true;
        issues = Json::array();
        for (const auto& [number, item] : items) {
            if (item["state"] == "open")
                issues.push_back(item);
        }
    }

    std::vector<Snowflake> inChannel(Snowflake channel) const
    {
        std::vector<Snowflake> out;
        for (const auto& [id, message] : messages) {
            if (message.first == channel)
                out.push_back(id);
        }
        return out;
    }
};

} // namespace

TEST(Cards, AnOpenPullRequestGetsACard)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    ASSERT_EQ(w.inChannel(Pulls).size(), 1u);
    EXPECT_TRUE(w.inChannel(Issues).empty());
    EXPECT_TRUE(w.state.cards.contains("pr:Afterglow#1"));
}

TEST(Cards, AnUnchangedRoundChangesNothing)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    w.calls.clear();
    w.cards.round();
    EXPECT_TRUE(w.calls.empty());
}

TEST(Cards, ADrawingChangeEditsInPlace)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    const Snowflake id = w.state.cards["pr:Afterglow#1"].message;
    w.items[1]["title"] = "Renamed";
    w.openPull(1, "s1");
    w.items[1]["title"] = "Renamed";
    w.issues[0]["title"] = "Renamed";
    w.calls.clear();
    w.cards.round();
    EXPECT_EQ(w.calls, (std::vector<std::string>{"PATCH"}));
    EXPECT_EQ(w.state.cards["pr:Afterglow#1"].message, id);
}

TEST(Cards, APushMovesTheCardToTheEnd)
{
    World w;
    w.openPull(1, "s1");
    w.openPull(2, "s2");
    w.cards.round();
    const Snowflake first = w.state.cards["pr:Afterglow#1"].message;
    w.pulls[1]["head"]["sha"] = "s1b";
    w.calls.clear();
    w.cards.round();
    EXPECT_EQ(w.calls, (std::vector<std::string>{"POST", "DELETE"}));
    const Snowflake moved = w.state.cards["pr:Afterglow#1"].message;
    EXPECT_GT(moved, w.state.cards["pr:Afterglow#2"].message);
    EXPECT_FALSE(w.messages.contains(first));
}

TEST(Cards, AFinishedRunMovesItButAStartedOneDoesNot)
{
    World w;
    w.openPull(1, "s1");
    w.runs = {{"workflow_runs", Json::array({{{"id", 1}, {"name", "Arch"}, {"status", "in_progress"}}})}};
    w.cards.round();
    w.calls.clear();
    w.runs["workflow_runs"][0]["status"] = "completed";
    w.runs["workflow_runs"][0]["conclusion"] = "failure";
    w.cards.round();
    EXPECT_EQ(w.calls, (std::vector<std::string>{"POST", "DELETE"}));
}

TEST(Cards, AClosedCardStaysADayThenGoes)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    w.close(1);
    w.cards.round();
    ASSERT_TRUE(w.state.cards.contains("pr:Afterglow#1"));
    const auto& card = w.state.cards["pr:Afterglow#1"];
    EXPECT_EQ(card.closedAt, w.now);
    const auto message = w.messages.at(card.message).second;
    EXPECT_EQ(message["embeds"][0]["color"], Embeds::Brand::Purple) << "merged";

    w.now += 23 * 3600;
    w.cards.round();
    EXPECT_EQ(w.inChannel(Pulls).size(), 1u);
    w.now += 3600;
    w.cards.round();
    EXPECT_TRUE(w.inChannel(Pulls).empty());
    EXPECT_FALSE(w.state.cards.contains("pr:Afterglow#1"));
}

TEST(Cards, ADeletedItemsCardGoes)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    w.items.erase(1);
    w.issues = Json::array();
    w.cards.round();
    EXPECT_TRUE(w.inChannel(Pulls).empty());
}

TEST(Cards, ACardSomeoneDeletedIsPostedAgain)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    w.messages.clear();
    w.items[1]["title"] = "Changed";
    w.issues[0]["title"] = "Changed";
    w.cards.round();
    EXPECT_EQ(w.inChannel(Pulls).size(), 1u);
}

TEST(Cards, TheFeedStartsQuietlyThenPostsEachPush)
{
    World w;
    w.releases = Json::array({{{"id", 1}, {"tag_name", "v0.1"}}});
    w.cards.round();
    EXPECT_TRUE(w.inChannel(Feed).empty()) << "the head and releases before Wisp are no news";
    w.head = "a2";
    w.cards.round();
    ASSERT_EQ(w.inChannel(Feed).size(), 1u);
    EXPECT_TRUE(w.state.cards.contains("push:Afterglow:a1..a2"));
    w.releases.insert(w.releases.begin(), Json{{"id", 2}, {"tag_name", "v0.2"}});
    w.cards.round();
    EXPECT_EQ(w.inChannel(Feed).size(), 2u);
    EXPECT_TRUE(w.state.cards.contains("release:Afterglow:2"));
}

TEST(Cards, CleanUpDeletesOnlyMyUnkeptMessages)
{
    World w;
    w.openPull(1, "s1");
    w.cards.round();
    w.messages[1] = {Pulls, {}};
    w.messages[2] = {Pulls, {}};
    // The fake's GET of messages: mine (author 9) and someone's (author 8).
    Discord discord{
        "t", "test",
        [&](const HttpRequest& request) {
            if (request.method == "GET" && request.url.find("/channels/100/") != std::string::npos) {
                return World::json(Json::array(
                    {{{"id", "1"}, {"author", {{"id", "9"}}}},
                     {{"id", "2"}, {"author", {{"id", "8"}}}},
                     {{"id", std::to_string(w.state.cards["pr:Afterglow#1"].message)}, {"author", {{"id", "9"}}}}}));
            }
            if (request.method == "GET")
                return World::json(Json::array());
            return w.discordCall(request);
        },
        [](std::chrono::milliseconds) { }};
    Cards cards{w.config, w.github, discord, w.state, [&] {
                    return w.now;
                }};
    cards.cleanUp(9);
    EXPECT_FALSE(w.messages.contains(1));
    EXPECT_TRUE(w.messages.contains(2));
    EXPECT_EQ(w.inChannel(Pulls).size(), 2u);
}

TEST(Cards, HashesAreStable)
{
    EXPECT_EQ(Cards::hash(Json{{"a", 1}}), Cards::hash(Json{{"a", 1}}));
    EXPECT_NE(Cards::hash(Json{{"a", 1}}), Cards::hash(Json{{"a", 2}}));
    EXPECT_EQ(Cards::hash(Json()).size(), 16u);
}
