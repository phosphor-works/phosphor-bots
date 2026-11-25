// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "State.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Wisp;

namespace {

State sample()
{
    State state;
    state.cards["pr:Afterglow#87"] = {1234567890123456789ULL, 2, "abc", "open|x", 100, 0};
    state.cards["issue:Afterglow#3"] = {11, 12, "def", "closed", 100, 200};
    state.replies.push_back({5, 6, 7, {{"Afterglow", 87}, {"PlasmaZones", 1}}, "h", 300});
    state.repos["Afterglow"] = {"deadbeef", {1, 2, 3}};
    state.latestComments["Afterglow#87"] = {87, "someone", "hi", "https://c", "2026-11-18T00:00:00Z"};
    return state;
}

std::filesystem::path scratch(const char* name)
{
    auto dir = std::filesystem::temp_directory_path() / ("wisp-state-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    return dir / name;
}

} // namespace

TEST(State, RoundTrip)
{
    EXPECT_EQ(stateFromJson(toJson(sample())), sample());
}

TEST(State, SnowflakesAreStrings)
{
    // JSON readers that read numbers as doubles lose a snowflake's last digits.
    EXPECT_TRUE(toJson(sample())["cards"]["pr:Afterglow#87"]["channel"].is_string());
}

TEST(State, SavedAndLoaded)
{
    const auto path = scratch("state.json");
    ASSERT_TRUE(saveState(path, sample()));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".new"));
    EXPECT_EQ(std::filesystem::status(path).permissions() & std::filesystem::perms::all,
              std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    const auto loaded = loadState(path);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(*loaded, sample());
    std::filesystem::remove_all(path.parent_path());
}

TEST(State, AMissingOrBrokenFileIsNoState)
{
    const auto path = scratch("broken.json");
    EXPECT_FALSE(loadState(path));
    std::ofstream(path) << "{not json";
    EXPECT_FALSE(loadState(path));
    std::ofstream(path) << R"({"format": 99})";
    EXPECT_FALSE(loadState(path));
    std::filesystem::remove_all(path.parent_path());
}
