// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace Wisp;

namespace {

std::string failure(const std::map<std::string, std::string>& env)
{
    try {
        parseConfig(env);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST(Config, TheTokenIsNeeded)
{
    EXPECT_NE(failure({}).find("WISP_TOKEN"), std::string::npos);
}

TEST(Config, Defaults)
{
    const Config config = parseConfig({{"WISP_TOKEN", "t"}, {"HOME", "/x"}});
    EXPECT_EQ(config.org, "phosphor-works");
    EXPECT_TRUE(config.repos.empty());
    EXPECT_FALSE(config.cardsWanted());
    EXPECT_EQ(config.pollInterval, std::chrono::seconds(30));
    EXPECT_EQ(config.maxReferences, 5u);
}

TEST(Config, EverySetting)
{
    const Config config = parseConfig({
        {"WISP_TOKEN", "t"},
        {"GITHUB_TOKEN", "g"},
        {"WISP_ORG", "someorg"},
        {"WISP_REPOS", "Afterglow, PlasmaZones"},
        {"WISP_ALIASES", "PZ=PlasmaZones ag=Afterglow"},
        {"WISP_DEFAULT_REPO", "Afterglow"},
        {"WISP_CHANNEL_REPOS", "111=PlasmaZones"},
        {"WISP_CHANNELS", "111,222"},
        {"WISP_CARDS_PRS", "333"},
        {"WISP_CARDS_ISSUES", "444"},
        {"WISP_FEED", "555"},
        {"WISP_STATE", "/var/db/wisp/state.json"},
        {"WISP_POLL_SECONDS", "60"},
        {"WISP_CLOSED_HOURS", "12"},
        {"WISP_REPLY_DAYS", "3"},
        {"WISP_MAX_REFERENCES", "3"},
        {"WISP_ICON_URL", "https://example.org/wisp.png"},
    });
    EXPECT_EQ(config.org, "someorg");
    EXPECT_EQ(config.repos, (std::vector<std::string>{"Afterglow", "PlasmaZones"}));
    EXPECT_EQ(config.aliases.at("pz"), "PlasmaZones");
    EXPECT_EQ(config.channelRepos.at(111), "PlasmaZones");
    EXPECT_EQ(config.channels, (std::set<Snowflake>{111, 222}));
    EXPECT_EQ(config.cardsPulls, 333u);
    EXPECT_EQ(config.cardsIssues, 444u);
    EXPECT_EQ(config.feed, 555u);
    EXPECT_EQ(config.pollInterval, std::chrono::seconds(60));
    EXPECT_EQ(config.closedKeep, std::chrono::hours(12));
    EXPECT_EQ(config.replyFollow, std::chrono::hours(72));
    EXPECT_EQ(config.maxReferences, 3u);
}

TEST(Config, MistakesStopIt)
{
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_CARD_PRS", "1"}}).find("no such setting"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_CHANNELS", "general"}}).find("channel id"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_CARDS_PRS", "1"}}).find("GITHUB_TOKEN"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_POLL_SECONDS", "1"}}).find("10 to 3600"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_ALIASES", "pz"}}).find("alias=Repository"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_REPOS", "a/b"}}).find("repository"), std::string::npos);
    EXPECT_NE(failure({{"WISP_TOKEN", "t"}, {"WISP_ICON_URL", "http://x"}}).find("https"), std::string::npos);
}
