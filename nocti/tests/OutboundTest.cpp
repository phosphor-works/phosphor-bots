// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Outbound.h"

#include <gtest/gtest.h>

using namespace Nocti::Outbound;

namespace {

std::string first(std::string_view text)
{
    const auto hits = check(text, "OWN.TOKEN.VALUE.OF.THE.TEST", "/home/someone");
    return hits.empty() ? std::string() : hits.front().rule;
}

} // namespace

// Every secret here is made up, in the shape of the real kind.
TEST(Outbound, SecretsStopADirectPost)
{
    EXPECT_EQ(first("MAAAAAAAAAAAAAAAAAAAAAAAAA.Abc_De.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"), "discord-token");
    EXPECT_EQ(first("use ghp_abcdefghijklmnopqrstuvwxyz0123456789"), "github-token");
    EXPECT_EQ(first("github_pat_ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"), "github-token");
    EXPECT_EQ(first("sk-ant-api03-abcdefghijklmnopqrstuv"), "api-key");
    EXPECT_EQ(first("AKIAABCDEFGHIJKLMNOP"), "aws-key");
    EXPECT_EQ(first("-----BEGIN OPENSSH PRIVATE KEY-----"), "private-key");
    EXPECT_EQ(first("password = hunter2hunter2hunter2"), "secret-value");
    EXPECT_EQ(first("NOCTI_OWNER=1"), "env-line");
    EXPECT_EQ(first("it is on 10.17.0.7:8090"), "private-address");
    EXPECT_EQ(first("try 127.0.0.1"), "private-address");
    EXPECT_EQ(first("mail me at someone@example.org"), "email");
    EXPECT_EQ(first("the token is OWN.TOKEN.VALUE.OF.THE.TEST"), "own-token");
    EXPECT_EQ(first("see /home/someone/Projects/x"), "local-path");
}

TEST(Outbound, PlainTextPasses)
{
    EXPECT_EQ(first("PlasmaZones#145 is fixed in v3.4.1: the zones no longer jump after resume."), "");
    EXPECT_EQ(first("Run `cmake --build build` and see docs/WISP.md, then @Glim."), "");
    EXPECT_EQ(first("Version 1.2.3.4 of the protocol"), "");
}

TEST(Outbound, AHitSaysWhere)
{
    const auto hits = check("line one\nkey = abcdefghijklmnopqrstu", "", "");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].line, 2);
    EXPECT_EQ(hits[0].column, 1);
}
