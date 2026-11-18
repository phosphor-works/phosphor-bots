// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Report.h"

#include <gtest/gtest.h>

using namespace Glim;
using namespace Glim::Report;

namespace {

constexpr Snowflake Owner = 100;
constexpr Snowflake Helper = 200;
constexpr Snowflake Staff = 300;
const std::string Url = "https://discord.com/channels/1/2";

Participants people()
{
    Participants p;
    p.reporter = Owner;
    return p;
}

bool has(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(Report, NamesAreReporterAndNumberedMembers)
{
    auto p = people();
    EXPECT_EQ(p.name(Owner), "Reporter");
    EXPECT_EQ(p.name(Helper), "Member 1");
    EXPECT_EQ(p.name(Staff), "Member 2");
    EXPECT_EQ(p.name(Helper), "Member 1") << "the same number each time";
}

TEST(Report, MentionsBecomeNames)
{
    auto p = people();
    EXPECT_EQ(anonymize("thanks <@200> and <@!100>", p), "thanks Member 1 and Reporter");
    EXPECT_EQ(anonymize("ping <@&555> in <#666> :) <:kde:777> <a:spin:888>", p),
              "ping @​role in #channel :) :kde: :spin:");
}

TEST(Report, GitHubMentionsAreInert)
{
    auto p = people();
    EXPECT_EQ(anonymize("cc @fuddlesworth and @phosphor-works/maintainers", p),
              "cc @​fuddlesworth and @​phosphor-works/maintainers");
    EXPECT_EQ(anonymize("mail me at a @ b", p), "mail me at a @ b");
}

TEST(Report, TextFilesAreRecognized)
{
    EXPECT_TRUE(isText({"kwin.log", "", 1, "", {}}));
    EXPECT_TRUE(isText({"x", "text/plain; charset=utf-8", 1, "", {}}));
    EXPECT_TRUE(isText({"config.JSON", "", 1, "", {}}));
    EXPECT_FALSE(isText({"shot.png", "image/png", 1, "", {}}));
    EXPECT_FALSE(isText({"core", "application/octet-stream", 1, "", {}}));
}

TEST(Report, AnIssueHoldsTheMessagesWithoutNames)
{
    auto p = people();
    const std::vector<Message> messages = {
        {1, Owner, "Zones jump on <@200>'s setup too", "2026-11-18T14:05:31.000000+00:00", {}},
        {2,
         Helper,
         "Same here",
         "2026-11-18T14:07:00.000000+00:00",
         {{"kwin.log", "text/plain", 12, "https://cdn/1", std::string("line ``` one\nline two")},
          {"shot.png", "image/png", 2000, "https://cdn/2", std::nullopt}}},
    };
    const std::string body = issueBody(Url, messages, p, Staff);
    EXPECT_TRUE(has(body, "at the request of Member 2")) << "the requester is numbered after the speakers";
    EXPECT_TRUE(has(body, "#### Reporter · 2026-11-18 14:05 UTC"));
    EXPECT_TRUE(has(body, "Zones jump on Member 1's setup too"));
    EXPECT_TRUE(has(body, "#### Member 1 · 2026-11-18 14:07 UTC"));
    EXPECT_TRUE(has(body, "<summary>📄 kwin.log (12 B)</summary>"));
    EXPECT_TRUE(has(body, "````text\nline ``` one\nline two\n````")) << "a fence longer than the log's backticks";
    EXPECT_TRUE(has(body, "📎 Attached in the Discord post: `shot.png`"));
    EXPECT_TRUE(has(body, Url));
}

TEST(Report, ALongLogIsCut)
{
    auto p = people();
    const std::string log(5000, 'x');
    const std::string body = issueBody(Url, {{1, Owner, "", "2026-11-18T14:05:31", {{"a.log", "", 5000, "", log}}}}, p,
                                       Owner, Limits{1000, 60000});
    EXPECT_TRUE(has(body, "(5 KiB), cut"));
    EXPECT_FALSE(has(body, std::string(1001, 'x')));
}

TEST(Report, TooManyMessagesPointToThePost)
{
    auto p = people();
    std::vector<Message> messages;
    for (int i = 0; i < 50; ++i)
        messages.push_back({Snowflake(i + 1), Owner, std::string(500, 'a'), "2026-11-18T14:05:31", {}});
    const std::string body = issueBody(Url, messages, p, Owner, Limits{1000, 5000});
    EXPECT_LE(body.size(), 5000u);
    EXPECT_TRUE(has(body, "more messages in the [Discord post]"));
}

TEST(Report, AnUpdateKeepsTheNumbers)
{
    auto p = people();
    issueBody(Url, {{1, Helper, "first", "2026-11-18T14:05:31", {}}}, p, Owner);
    const std::string comment = updateComment(
        Url, {{5, Staff, "new", "2026-11-18T15:00:00", {}}, {6, Helper, "again", "2026-11-18T15:01:00", {}}}, p, Owner);
    EXPECT_TRUE(has(comment, "#### Member 2"));
    EXPECT_TRUE(has(comment, "#### Member 1"));
    EXPECT_TRUE(has(comment, "since the last update"));
}

TEST(Report, CutKeepsCharactersWhole)
{
    EXPECT_EQ(cut("aaé", 3), "aa");
    EXPECT_EQ(cut("short", 10), "short");
}
