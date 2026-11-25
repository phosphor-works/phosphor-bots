// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Discord.h"

#include <gtest/gtest.h>

#include <deque>

using namespace Bots;
using std::chrono::milliseconds;

namespace {

struct Fake
{
    std::deque<HttpResponse> answers;
    std::vector<HttpRequest> requests;
    std::vector<milliseconds> sleeps;

    Discord discord()
    {
        return Discord(
            "TOKEN", "test/1",
            [this](const HttpRequest& request) {
                requests.push_back(request);
                HttpResponse answer = answers.empty() ? HttpResponse{200, {}, "{}", {}} : answers.front();
                if (!answers.empty())
                    answers.pop_front();
                return answer;
            },
            [this](milliseconds d) {
                sleeps.push_back(d);
            });
    }
};

} // namespace

TEST(Discord, RouteKeysKeepTheMajorParameters)
{
    EXPECT_EQ(Discord::routeKey("POST", "/channels/123/messages"), "POST /channels/123/messages");
    EXPECT_EQ(Discord::routeKey("PATCH", "/channels/123/messages/456"), "PATCH /channels/123/messages/{id}");
    EXPECT_EQ(Discord::routeKey("DELETE", "/channels/123/messages/456"), "DELETE /channels/123/messages/{id}#delete");
    EXPECT_EQ(Discord::routeKey("GET", "/channels/9/messages?limit=100"), "GET /channels/9/messages");
    EXPECT_EQ(Discord::routeKey("GET", "/gateway/bot"), "GET /gateway/bot");
}

TEST(Discord, ARepliesPingsNoOne)
{
    Fake fake;
    auto discord = fake.discord();
    discord.send(10, {{"content", "hi"}}, 20);
    ASSERT_EQ(fake.requests.size(), 1u);
    const auto body = nlohmann::json::parse(fake.requests[0].body);
    EXPECT_EQ(body["allowed_mentions"]["parse"], nlohmann::json::array());
    EXPECT_EQ(body["allowed_mentions"]["replied_user"], false);
    EXPECT_EQ(body["message_reference"]["message_id"], "20");
    EXPECT_EQ(fake.requests[0].url, "https://discord.com/api/v10/channels/10/messages");
    EXPECT_NE(std::find(fake.requests[0].headers.begin(), fake.requests[0].headers.end(), "Authorization: Bot TOKEN"),
              fake.requests[0].headers.end());
}

TEST(Discord, A429WaitsItsRetryAfterAndTriesAgain)
{
    Fake fake;
    fake.answers.push_back({429, {}, R"({"retry_after": 1.5, "global": false})", {}});
    fake.answers.push_back({200, {}, R"({"id": "77"})", {}});
    auto discord = fake.discord();
    const auto result = discord.send(10, {{"content", "hi"}});
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.body["id"], "77");
    EXPECT_EQ(fake.requests.size(), 2u);
    ASSERT_EQ(fake.sleeps.size(), 1u);
    EXPECT_GE(fake.sleeps[0], milliseconds(1400));
    EXPECT_LE(fake.sleeps[0], milliseconds(1500));
}

TEST(Discord, AnEmptyBucketWaitsBeforeTheNextCallOnItsRoute)
{
    Fake fake;
    fake.answers.push_back({200, {{"x-ratelimit-remaining", "0"}, {"x-ratelimit-reset-after", "2"}}, "{}", {}});
    fake.answers.push_back({200, {}, "{}", {}});
    fake.answers.push_back({200, {}, "{}", {}});
    auto discord = fake.discord();
    discord.send(10, {{"content", "a"}});
    discord.send(11, {{"content", "b"}}); // another channel: another bucket
    EXPECT_TRUE(fake.sleeps.empty());
    discord.send(10, {{"content", "c"}});
    ASSERT_EQ(fake.sleeps.size(), 1u);
    EXPECT_GT(fake.sleeps[0], milliseconds(1500));
}

TEST(Discord, ServerErrorsAreTriedAgainAFewTimes)
{
    Fake fake;
    for (int i = 0; i < 10; ++i)
        fake.answers.push_back({502, {}, "", {}});
    auto discord = fake.discord();
    const auto result = discord.remove(1, 2);
    EXPECT_EQ(result.status, 502);
    EXPECT_EQ(fake.requests.size(), 4u);
}

TEST(Discord, ClientErrorsAreNotTriedAgain)
{
    Fake fake;
    fake.answers.push_back({404, {}, R"({"message": "Unknown Message"})", {}});
    auto discord = fake.discord();
    const auto result = discord.edit(1, 2, {{"content", "x"}});
    EXPECT_EQ(result.status, 404);
    EXPECT_EQ(fake.requests.size(), 1u);
}

TEST(Discord, Snowflakes)
{
    EXPECT_EQ(parseSnowflake("1234567890123456789"), 1234567890123456789ULL);
    EXPECT_FALSE(parseSnowflake(""));
    EXPECT_FALSE(parseSnowflake("0"));
    EXPECT_FALSE(parseSnowflake("12a"));
    EXPECT_FALSE(parseSnowflake("-1"));
}
