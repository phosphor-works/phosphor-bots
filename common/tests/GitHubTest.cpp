// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GitHub.h"

#include <gtest/gtest.h>

using namespace Bots;
using Json = nlohmann::json;

TEST(GitHub, AnUnchangedAnswerComesFromItsETag)
{
    std::vector<HttpRequest> requests;
    int calls = 0;
    GitHub github("org", "TOKEN", "test/1", [&](const HttpRequest& request) {
        requests.push_back(request);
        if (calls++ == 0)
            return HttpResponse{200, {{"etag", "\"abc\""}}, R"([{"name": "Afterglow"}])", {}};
        return HttpResponse{304, {}, "", {}};
    });
    const auto first = github.get("/orgs/org/repos");
    const auto second = github.get("/orgs/org/repos");
    ASSERT_TRUE(first && second);
    EXPECT_EQ(*first, *second);
    ASSERT_EQ(requests.size(), 2u);
    const auto& headers = requests[1].headers;
    EXPECT_NE(std::find(headers.begin(), headers.end(), "If-None-Match: \"abc\""), headers.end());
    EXPECT_NE(std::find(headers.begin(), headers.end(), "Authorization: Bearer TOKEN"), headers.end());
}

TEST(GitHub, AnItemThatMovedIsGone)
{
    GitHub github("org", "", "test/1", [](const HttpRequest&) {
        return HttpResponse{404, {}, "{}", {}};
    });
    bool gone = false;
    EXPECT_FALSE(github.item("Afterglow", 5, &gone));
    EXPECT_TRUE(gone);

    GitHub down("org", "", "test/1", [](const HttpRequest&) {
        return HttpResponse{0, {}, "", "timeout"};
    });
    gone = true;
    EXPECT_FALSE(down.item("Afterglow", 5, &gone));
    EXPECT_FALSE(gone) << "no answer is not gone";
}

TEST(GitHub, APullRequestHasItsOwnFields)
{
    GitHub github("org", "", "test/1", [](const HttpRequest& request) {
        if (request.url.ends_with("/issues/87"))
            return HttpResponse{200,
                                {},
                                R"({"number": 87, "title": "Port", "state": "open",
                "pull_request": {"merged_at": null}, "user": {"login": "someone"},
                "labels": [{"name": "freebsd", "color": "ff0000"}]})",
                                {}};
        return HttpResponse{200,
                            {},
                            R"({"merged": false, "draft": true, "commits": 6, "additions": 10,
            "deletions": 2, "changed_files": 3, "head": {"ref": "port", "label": "o:port", "sha": "abc"},
            "base": {"ref": "main"}})",
                            {}};
    });
    const auto item = github.item("Afterglow", 87);
    ASSERT_TRUE(item);
    EXPECT_TRUE(item->pull);
    EXPECT_TRUE(item->draft);
    EXPECT_EQ(item->author, "someone");
    EXPECT_EQ(item->headSha, "abc");
    EXPECT_EQ(item->baseRef, "main");
    EXPECT_EQ(item->commits, 6);
    ASSERT_EQ(item->labels.size(), 1u);
    EXPECT_EQ(item->labels[0].name, "freebsd");
}

TEST(GitHub, OnlyTheNewestRunOfEachWorkflow)
{
    const Json runs = Json::parse(R"({"workflow_runs": [
        {"id": 3, "name": "Nix", "status": "in_progress", "conclusion": null},
        {"id": 2, "name": "Arch", "status": "completed", "conclusion": "success"},
        {"id": 1, "name": "Nix", "status": "completed", "conclusion": "failure"}]})");
    const auto parsed = GitHub::parseRuns(runs);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].name, "Arch");
    EXPECT_EQ(parsed[1].name, "Nix");
    EXPECT_EQ(parsed[1].id, 3);
}

TEST(GitHub, ABotsCommentIsNoPersonsComment)
{
    EXPECT_FALSE(GitHub::parseComment(Json::parse(R"({"user": {"login": "github-actions[bot]", "type": "Bot"}})")));
    const auto comment = GitHub::parseComment(Json::parse(
        R"({"user": {"login": "someone", "type": "User"}, "body": "hi",
            "issue_url": "https://api.github.com/repos/o/r/issues/12"})"));
    ASSERT_TRUE(comment);
    EXPECT_EQ(comment->issue, 12);
    EXPECT_EQ(comment->author, "someone");
}
