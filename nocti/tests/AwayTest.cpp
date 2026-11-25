// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Away.h"

#include <gtest/gtest.h>

#include <filesystem>

#include <unistd.h>

using namespace Nocti;

TEST(Away, Spans)
{
    EXPECT_EQ(parseSpan("30m"), 1800);
    EXPECT_EQ(parseSpan("8h"), 8 * 3600);
    EXPECT_EQ(parseSpan("7d"), 7 * 86400);
    for (const char* bad : {"8d", "0h", "h", "10", "1.5h", "30s", "-1h", ""})
        EXPECT_FALSE(parseSpan(bad)) << bad;
    EXPECT_EQ(parseMode("asleep"), Mode::Asleep);
    EXPECT_FALSE(parseMode("away"));
}

TEST(Away, ATimedModeRunsOutOnce)
{
    std::int64_t now = 1'000'000;
    Away away("", [&] {
        return now;
    });
    EXPECT_EQ(away.setMode(Mode::Work, 3600), Mode::Home);
    EXPECT_TRUE(away.away());
    EXPECT_EQ(away.modeUntil(), now + 3600);
    now += 3600;
    EXPECT_EQ(away.mode(), Mode::Home);
    EXPECT_EQ(away.ranOut(), Mode::Work);
    EXPECT_FALSE(away.ranOut()) << "once";
    away.setMode(Mode::Home, 3600);
    EXPECT_EQ(away.modeUntil(), 0) << "home has no end";
}

TEST(Away, AnApproversTimeRunsOutOnce)
{
    std::int64_t now = 1'000'000;
    Away away("", [&] {
        return now;
    });
    EXPECT_FALSE(away.clearApprover());
    away.setApprover(5, 7200);
    EXPECT_EQ(away.approver(), 5u);
    now += 7200;
    EXPECT_EQ(away.approver(), 0u);
    EXPECT_EQ(away.approverRanOut(), 5u);
    EXPECT_FALSE(away.approverRanOut());
}

TEST(Away, CancelSaysHowAPostLeft)
{
    std::int64_t now = 1'000'000;
    Away away("", [&] {
        return now;
    });
    away.setMode(Mode::Work, 0);
    EXPECT_EQ(away.hold(1, 0, "a"), "h1");
    EXPECT_EQ(away.queue(1, 2, "b"), "q2");
    EXPECT_EQ(away.takeQueued("posted")->text, "b");
    EXPECT_EQ(away.cancel("q2"), "posted");
    EXPECT_EQ(away.cancel("h1"), "");
    EXPECT_EQ(away.cancel("h1"), "taken back");
    EXPECT_EQ(away.cancel("zz"), "no such post");
    now += 86401;
    EXPECT_EQ(away.cancel("q2"), "no such post") << "kept a day";
}

TEST(Away, KeptBetweenRuns)
{
    const auto path = std::filesystem::temp_directory_path() / ("nocti-away-" + std::to_string(::getpid()) + ".json");
    std::int64_t now = 1'000'000;
    {
        Away away(path.string(), [&] {
            return now;
        });
        away.setMode(Mode::Asleep, 0);
        away.hold(7, 8, "held one");
        away.queue(7, 0, "queued one");
        away.setApprover(5, 7200);
    }
    Away again(path.string(), [&] {
        return now;
    });
    EXPECT_EQ(again.mode(), Mode::Asleep);
    ASSERT_EQ(again.held().size(), 1u);
    EXPECT_EQ(again.held()[0].mode, Mode::Asleep);
    EXPECT_EQ(again.held()[0].replyTo, 8u);
    EXPECT_EQ(again.queued().size(), 1u);
    EXPECT_EQ(again.approver(), 5u);
    EXPECT_EQ(again.hold(7, 0, "next"), "h3") << "the marks go on";
    EXPECT_EQ(again.takeHeld("a draft").size(), 2u);
    std::filesystem::remove(path);
}
