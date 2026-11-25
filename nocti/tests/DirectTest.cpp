// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Direct.h"

#include <gtest/gtest.h>

#include <filesystem>

#include <unistd.h>

using namespace Nocti;

namespace {

Target from(Level level, Snowflake message = 1)
{
    Target target;
    target.message = message;
    target.author = 7;
    target.level = level;
    return target;
}

} // namespace

TEST(Direct, ThreeRepliesToTheOwnersOrATrustedMessage)
{
    std::int64_t now = 1'000'000;
    DirectPolicy policy(std::chrono::minutes(30), "", [&] {
        return now;
    });
    for (const Level level : {Level::Owner, Level::Trusted}) {
        const Target target = from(level, level == Level::Owner ? 1 : 2);
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(policy.refusal(target), "");
            EXPECT_EQ(policy.left(target), 3 - i);
            policy.record(target);
        }
        EXPECT_NE(policy.refusal(target).find("3 replies"), std::string::npos);
    }
    now += 86400;
    EXPECT_EQ(policy.refusal(from(Level::Owner, 1)), "") << "a day later the count is gone";
}

TEST(Direct, AllowedAndOwnPostsTakeTheInterval)
{
    std::int64_t now = 1'000'000;
    DirectPolicy policy(std::chrono::minutes(30), "", [&] {
        return now;
    });
    EXPECT_EQ(policy.refusal(std::nullopt), "");
    policy.record(std::nullopt);
    EXPECT_NE(policy.refusal(from(Level::Allowed)).find("the interval"), std::string::npos) << "one interval for both";
    EXPECT_NE(policy.describe(std::nullopt).find("direct from"), std::string::npos);
    now += 1800;
    EXPECT_EQ(policy.refusal(from(Level::Allowed)), "");
    EXPECT_EQ(policy.describe(from(Level::Owner)), "direct, 3 left");
}

TEST(Direct, StrangersCautionsAndOtherDmsAreDraftsOnly)
{
    DirectPolicy policy(std::chrono::minutes(30), "");
    EXPECT_NE(policy.refusal(from(Level::Stranger)).find("stranger"), std::string::npos);
    Target cautioned = from(Level::Trusted);
    cautioned.caution = true;
    EXPECT_NE(policy.refusal(cautioned).find("caution"), std::string::npos);
    Target owners = from(Level::Owner);
    owners.caution = true;
    EXPECT_EQ(policy.refusal(owners), "") << "the owner's own caution does not stop his answer";
    Target dm = from(Level::Trusted);
    dm.dm = true;
    EXPECT_NE(policy.refusal(dm).find("DM"), std::string::npos);
    dm.ownerDm = true;
    EXPECT_EQ(policy.refusal(dm), "");
}

TEST(Direct, KeptBetweenRuns)
{
    const auto path = std::filesystem::temp_directory_path() / ("nocti-direct-" + std::to_string(::getpid()) + ".json");
    std::int64_t now = 1'000'000;
    {
        DirectPolicy policy(std::chrono::minutes(30), path.string(), [&] {
            return now;
        });
        policy.record(std::nullopt);
        policy.record(from(Level::Owner, 5));
    }
    DirectPolicy again(std::chrono::minutes(30), path.string(), [&] {
        return now;
    });
    EXPECT_NE(again.refusal(std::nullopt).find("the interval"), std::string::npos);
    EXPECT_EQ(again.left(from(Level::Owner, 5)), 2);
    std::filesystem::remove(path);
}
