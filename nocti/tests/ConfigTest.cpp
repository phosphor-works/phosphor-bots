// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <sys/stat.h>
#include <unistd.h>

using namespace Nocti;

namespace {

std::string failure(const std::function<void()>& run)
{
    try {
        run();
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST(NoctiConfig, EnvText)
{
    const auto values = parseEnvText("# a comment\n\nNOCTI_TOKEN = \"abc\"\nNOCTI_OWNER=42\r\n");
    EXPECT_EQ(values.at("NOCTI_TOKEN"), "abc");
    EXPECT_EQ(values.at("NOCTI_OWNER"), "42");
    EXPECT_NE(failure([] {
                  parseEnvText("A=1\nA=2\n");
              }).find("set twice"),
              std::string::npos);
    EXPECT_NE(failure([] {
                  parseEnvText("just words\n");
              }).find("line 1"),
              std::string::npos);
}

TEST(NoctiConfig, Settings)
{
    const Config config = parseConfig(
        {{"NOCTI_TOKEN", "t"}, {"NOCTI_OWNER", "42"}, {"NOCTI_TRUSTED", "7, 42,8"}, {"NOCTI_CHANNELS", "100"}});
    EXPECT_EQ(config.owner, 42u);
    EXPECT_EQ(config.trusted, (std::set<Snowflake>{7, 8})) << "the owner is no trusted one";
    EXPECT_EQ(config.channels, (std::set<Snowflake>{100}));
    EXPECT_NE(failure([] {
                  parseConfig({{"NOCTI_OWNER", "42"}});
              }).find("NOCTI_TOKEN"),
              std::string::npos);
    EXPECT_NE(failure([] {
                  parseConfig({{"NOCTI_TOKEN", "t"}});
              }).find("NOCTI_OWNER"),
              std::string::npos);
    EXPECT_NE(failure([] {
                  parseConfig({{"NOCTI_TOKEN", "t"}, {"NOCTI_OWNER", "42"}, {"NOCTI_USERS", "1"}});
              }).find("no such setting"),
              std::string::npos);
    EXPECT_NE(failure([] {
                  parseConfig({{"NOCTI_TOKEN", "t"}, {"NOCTI_OWNER", "42"}, {"NOCTI_TRUSTED", "bob"}});
              }).find("not an id"),
              std::string::npos);
}

TEST(NoctiConfig, AFileOthersMayReadIsRefused)
{
    const auto path = std::filesystem::temp_directory_path() / ("nocti-env-" + std::to_string(::getpid()));
    std::ofstream(path) << "NOCTI_TOKEN=t\nNOCTI_OWNER=42\n";
    ::chmod(path.c_str(), 0644);
    EXPECT_NE(failure([&] {
                  loadConfig(path.string());
              }).find("mode 644"),
              std::string::npos);
    ::chmod(path.c_str(), 0600);
    EXPECT_EQ(loadConfig(path.string()).owner, 42u);
    std::filesystem::remove(path);
    EXPECT_NE(failure([&] {
                  loadConfig(path.string());
              }).find("no such file"),
              std::string::npos);
}
