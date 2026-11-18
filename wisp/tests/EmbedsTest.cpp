// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Embeds.h"

#include <gtest/gtest.h>

using namespace Wisp;
using namespace Wisp::Embeds;

namespace {

const Look look{"phosphor-works", "https://example.org/wisp.png", "https://example.org/org.png"};

Item pull()
{
    Item item;
    item.repo = "Afterglow";
    item.number = 87;
    item.pull = true;
    item.title = "Port aglow to FreeBSD";
    item.url = "https://github.com/phosphor-works/Afterglow/pull/87";
    item.state = "open";
    item.author = "someone";
    item.headLabel = "phosphor-works:port/freebsd";
    item.baseRef = "main";
    item.commits = 6;
    item.additions = 900;
    item.deletions = 40;
    item.changedFiles = 31;
    item.labels = {{"freebsd", "00ff00"}};
    item.updatedAt = "2026-11-18T10:00:00Z";
    return item;
}

} // namespace

TEST(Embeds, EachKindHasItsColour)
{
    Item item = pull();
    EXPECT_EQ(itemColor(item), KindColor::PullRequest);
    item.pull = false;
    EXPECT_EQ(itemColor(item), KindColor::Issue);
    EXPECT_NE(KindColor::PullRequest, KindColor::Issue);
    EXPECT_NE(KindColor::Push, KindColor::Release);
}

TEST(Embeds, DraftsAndFinishedItemsAreMuted)
{
    Item item = pull();
    item.draft = true;
    EXPECT_EQ(itemColor(item), KindColor::Finished);
    item.draft = false;
    item.state = "closed";
    item.merged = true;
    EXPECT_EQ(itemColor(item), KindColor::Finished);
    EXPECT_EQ(itemState(item), "merged pull request");
    item.pull = false;
    item.merged = false;
    item.stateReason = "not_planned";
    EXPECT_EQ(itemColor(item), KindColor::Finished);
    EXPECT_EQ(itemState(item), "closed (not planned) issue");
}

TEST(Embeds, ACardCarriesTheBrand)
{
    Embeds::CardExtras extras;
    extras.runs = {{1, "Arch", "completed", "success", "https://ci/1", "abc", ""},
                   {2, "Nix", "in_progress", "", "https://ci/2", "abc", ""}};
    extras.latestComment = Comment{87, "reviewer", "Looks good.\n<!-- hidden -->\nShip it", "https://c", ""};
    const auto card = itemCard(look, pull(), extras);
    const auto& embed = card["embeds"][0];
    EXPECT_EQ(embed["title"], "#87 · Port aglow to FreeBSD");
    EXPECT_EQ(embed["color"], KindColor::PullRequest);
    EXPECT_EQ(embed["author"]["name"], "phosphor-works / Afterglow");
    EXPECT_EQ(embed["author"]["icon_url"], "https://example.org/org.png");
    EXPECT_EQ(embed["footer"]["text"], "Wisp · phosphor-works");
    EXPECT_EQ(embed["footer"]["icon_url"], "https://example.org/wisp.png");
    EXPECT_EQ(embed["timestamp"], "2026-11-18T10:00:00Z");
    const std::string description = embed["description"];
    EXPECT_NE(description.find("6 commits · +900 −40 in 31 files"), std::string::npos);
    EXPECT_NE(description.find("✅ [Arch](https://ci/1) · ⏳ [Nix](https://ci/2)"), std::string::npos);
    EXPECT_NE(description.find("> Looks good."), std::string::npos);
    EXPECT_EQ(description.find("hidden"), std::string::npos);
}

TEST(Embeds, NoIconWithoutAnAddress)
{
    const auto card = itemCard({"phosphor-works", "", ""}, pull(), {});
    EXPECT_FALSE(card["embeds"][0]["footer"].contains("icon_url"));
    EXPECT_FALSE(card["embeds"][0]["author"].contains("icon_url"));
}

TEST(Embeds, RunResults)
{
    EXPECT_EQ(runsResult({}), "");
    EXPECT_EQ(runsResult({{1, "A", "completed", "success", "", "", ""}}), "success");
    EXPECT_EQ(runsResult({{1, "A", "completed", "success", "", "", ""}, {2, "B", "queued", "", "", "", ""}}),
              "pending");
    EXPECT_EQ(runsResult({{1, "A", "completed", "failure", "", "", ""}, {2, "B", "queued", "", "", "", ""}}),
              "failure");
    EXPECT_EQ(runsResult({{1, "A", "completed", "cancelled", "", "", ""}}), "success") << "nothing failed";
}

TEST(Embeds, APushCard)
{
    Push push;
    push.repo = "Afterglow";
    push.branch = "main";
    push.before = "1111111111111111";
    push.head = {"2222222222222222", "Second", "b", "https://c/2"};
    push.commits = {{"1212121212", "First", "a", "https://c/1"}, push.head};
    push.total = 2;
    push.runs = {{1, "Arch", "completed", "failure", "https://ci", "", ""}};
    const auto embed = pushCard(look, push)["embeds"][0];
    EXPECT_EQ(embed["title"], "2 commits to main");
    EXPECT_EQ(embed["color"], KindColor::Push) << "a failed run shows in its icon";
    EXPECT_EQ(embed["url"], "https://github.com/phosphor-works/Afterglow/compare/111111111111...222222222222");
    const std::string description = embed["description"];
    EXPECT_LT(description.find("Second"), description.find("First")) << "newest first";
}

TEST(Embeds, AReleaseCard)
{
    Release release;
    release.tag = "v0.1.0";
    release.url = "https://r";
    release.body = "Notes";
    release.assets = {{"afterglow.tar.gz", 3 * 1024 * 1024, "https://a"}};
    const auto embed = releaseCard(look, "Afterglow", release)["embeds"][0];
    EXPECT_EQ(embed["title"], "Afterglow v0.1.0");
    EXPECT_EQ(embed["color"], KindColor::Release);
    EXPECT_NE(std::string(embed["description"]).find("afterglow.tar.gz](https://a) (3.0 MiB)"), std::string::npos);
}

TEST(Embeds, TextIsEscapedAndCutWhole)
{
    EXPECT_EQ(escape("*bold* _x_ `c`"), "\\*bold\\* \\_x\\_ \\`c\\`");
    EXPECT_EQ(shorten("short", 10), "short");
    // é is two bytes, at 6 and 7: the cut at 7 moves back before it.
    EXPECT_EQ(shorten("aaaaaaébbbb", 10), "aaaaaa…");
    EXPECT_EQ(shorten("aaaaaaaaaaaa", 10), "aaaaaaa…");
}

TEST(Embeds, AReplyHasAnEmbedForEachItem)
{
    Item issue = pull();
    issue.pull = false;
    issue.number = 3;
    const auto reply = Embeds::reply(look, {pull(), issue});
    ASSERT_EQ(reply["embeds"].size(), 2u);
    EXPECT_EQ(reply["embeds"][1]["title"], "#3 · Port aglow to FreeBSD");
}
