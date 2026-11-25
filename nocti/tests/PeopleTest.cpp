// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "People.h"

#include <gtest/gtest.h>

#include <filesystem>

#include <unistd.h>

using namespace Nocti;

TEST(People, Levels)
{
    std::int64_t now = 1'000'000;
    People people(1, {2}, "", [&] {
        return now;
    });
    EXPECT_EQ(people.level(1), Level::Owner);
    EXPECT_EQ(people.level(2), Level::Trusted);
    EXPECT_EQ(people.level(3), Level::Stranger);
    people.allow(3, "Sam", 3600);
    EXPECT_EQ(people.level(3), Level::Allowed);
    EXPECT_EQ(people.describe(3), "allowed until " + utcTime(now + 3600));
    now += 3600;
    EXPECT_EQ(people.level(3), Level::Stranger) << "the allow ran out";
}

TEST(People, Weights)
{
    std::int64_t now = 1'000'000;
    People people(1, {2}, "", [&] {
        return now;
    });
    people.allow(3, "Sam", 0);
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(people.take(3));
    EXPECT_FALSE(people.take(3)) << "10 an hour for someone allowed";
    EXPECT_EQ(people.heard(3), "10 of 10 this hour");
    now += 3600;
    EXPECT_TRUE(people.take(3));
    for (int i = 0; i < 100; ++i)
        EXPECT_TRUE(people.take(1)) << "the owner is never limited";
    EXPECT_FALSE(people.take(9)) << "a stranger has no weight";
}

TEST(People, KeptBetweenRuns)
{
    const auto path = std::filesystem::temp_directory_path() / ("nocti-people-" + std::to_string(::getpid()) + ".json");
    {
        People people(1, {2}, path.string());
        people.untrust(2);
        people.trust(4);
        people.allow(5, "Kim", 0);
    }
    People again(1, {2}, path.string());
    EXPECT_EQ(again.level(2), Level::Stranger) << "untrusted though NOCTI_TRUSTED lists them";
    EXPECT_EQ(again.level(4), Level::Trusted);
    EXPECT_EQ(again.level(5), Level::Allowed);
    EXPECT_EQ(std::filesystem::status(path).permissions() & std::filesystem::perms::all,
              std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::remove(path);
}
