// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace Glim;

namespace {

std::map<std::string, std::string> base()
{
    return {{"GLIM_TOKEN", "t"},
            {"GLIM_APP_ID", "123"},
            {"GLIM_APP_KEY", "/usr/local/etc/glim/app.pem"},
            {"GLIM_FORUMS", "1526342492029587548"}};
}

std::string failure(std::map<std::string, std::string> env)
{
    try {
        parseConfig(env);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST(GlimConfig, TheNeededKeys)
{
    for (const char* key : {"GLIM_TOKEN", "GLIM_APP_ID", "GLIM_APP_KEY", "GLIM_FORUMS"}) {
        auto env = base();
        env.erase(key);
        EXPECT_NE(failure(env).find(key), std::string::npos) << key;
    }
}

TEST(GlimConfig, Defaults)
{
    const Config config = parseConfig(base());
    EXPECT_EQ(config.org, "phosphor-works");
    EXPECT_EQ(config.solvedTag, "solved");
    EXPECT_EQ(config.pollInterval, std::chrono::seconds(300));
    EXPECT_EQ(config.forums.size(), 1u);
}

TEST(GlimConfig, EverySetting)
{
    auto env = base();
    env["GLIM_TAG_REPOS"] = "plasmazones=PlasmaZones Afterglow=Afterglow";
    env["GLIM_ALIASES"] = "pz=PlasmaZones";
    env["GLIM_STAFF_ROLES"] = "1,2,3";
    env["GLIM_SOLVED_TAG"] = "";
    env["GLIM_LABELS"] = "from-discord triage";
    env["GLIM_POLL_SECONDS"] = "120";
    env["GLIM_TEXT_LIMIT"] = "2048";
    const Config config = parseConfig(env);
    EXPECT_EQ(config.tagRepos.at("afterglow"), "Afterglow");
    EXPECT_EQ(config.aliases.at("pz"), "PlasmaZones");
    EXPECT_EQ(config.staffRoles.size(), 3u);
    EXPECT_TRUE(config.solvedTag.empty());
    EXPECT_EQ(config.labels, (std::vector<std::string>{"from-discord", "triage"}));
    EXPECT_EQ(config.textLimit, 2048u);
}

TEST(GlimConfig, Mistakes)
{
    auto env = base();
    env["GLIM_FORUM"] = "1";
    EXPECT_NE(failure(env).find("no such setting"), std::string::npos);
    env = base();
    env["GLIM_APP_KEY"] = "app.pem";
    EXPECT_NE(failure(env).find("absolute"), std::string::npos);
    env = base();
    env["GLIM_APP_ID"] = "glim";
    EXPECT_NE(failure(env).find("GLIM_APP_ID"), std::string::npos);
    env = base();
    env["GLIM_TAG_REPOS"] = "plasmazones";
    EXPECT_NE(failure(env).find("name=Repository"), std::string::npos);
}
