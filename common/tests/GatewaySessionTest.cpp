// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GatewaySession.h"

#include <gtest/gtest.h>

using namespace Bots;
using Json = nlohmann::json;
using std::chrono::milliseconds;

namespace {

using TimePoint = GatewaySession::TimePoint;

const TimePoint T0{};

GatewaySession session()
{
    return GatewaySession("TOKEN", "testbot", [] {
        return 0.5;
    });
}

Json op(const std::string& payload)
{
    return Json::parse(payload);
}

std::string hello(long long interval = 40000)
{
    return Json{{"op", 10}, {"d", {{"heartbeat_interval", interval}}}}.dump();
}

std::string ready(long long seq = 1)
{
    return Json{{"op", 0},
                {"s", seq},
                {"t", "READY"},
                {"d",
                 {{"session_id", "SESSION"},
                  {"resume_gateway_url", "wss://resume.example"},
                  {"user", {{"id", "42"}, {"username", "wisp"}}}}}}
        .dump();
}

} // namespace

TEST(GatewaySession, HelloIdentifiesWithTheIntents)
{
    auto s = session();
    s.connected(T0);
    const auto out = s.receive(hello(), T0);
    ASSERT_EQ(out.size(), 1u);
    const Json identify = op(out[0]);
    EXPECT_EQ(identify["op"], 2);
    EXPECT_EQ(identify["d"]["token"], "TOKEN");
    EXPECT_EQ(identify["d"]["intents"], GatewaySession::Intents);
    EXPECT_EQ(identify["d"]["properties"]["browser"], "testbot");
    EXPECT_NE(GatewaySession::Intents & (1 << 15), 0) << "Message Content";
}

TEST(GatewaySession, TheFirstHeartbeatComesAfterAJitteredPartOfTheInterval)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(40000), T0);
    EXPECT_EQ(s.nextDeadline(), T0 + milliseconds(20000));
    EXPECT_TRUE(s.tick(T0 + milliseconds(19999)).empty());
    const auto beat = s.tick(T0 + milliseconds(20000));
    ASSERT_EQ(beat.size(), 1u);
    EXPECT_EQ(op(beat[0])["op"], 1);
    EXPECT_TRUE(op(beat[0])["d"].is_null()) << "no sequence yet";
}

TEST(GatewaySession, HeartbeatsCarryTheLastSequence)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(40000), T0);
    s.receive(ready(7), T0);
    const auto beat = s.tick(T0 + milliseconds(20000));
    ASSERT_EQ(beat.size(), 1u);
    EXPECT_EQ(op(beat[0])["d"], 7);
}

TEST(GatewaySession, AnUnacknowledgedHeartbeatEndsTheConnectionButKeepsTheSession)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(40000), T0);
    s.receive(ready(), T0);
    s.tick(T0 + milliseconds(20000));
    EXPECT_FALSE(s.mustReconnect());
    EXPECT_TRUE(s.tick(T0 + milliseconds(60000)).empty());
    EXPECT_TRUE(s.mustReconnect());
    EXPECT_EQ(s.connectUrl("wss://gateway.example"), "wss://resume.example/?v=10&encoding=json");
}

TEST(GatewaySession, AnAcknowledgedHeartbeatKeepsItGoing)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(40000), T0);
    s.receive(ready(), T0);
    s.tick(T0 + milliseconds(20000));
    s.receive(R"({"op":11})", T0 + milliseconds(20100));
    EXPECT_EQ(s.tick(T0 + milliseconds(60000)).size(), 1u);
    EXPECT_FALSE(s.mustReconnect());
}

TEST(GatewaySession, ANewConnectionResumes)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(), T0);
    s.receive(ready(5), T0);
    s.closed(1001);
    ASSERT_TRUE(s.mustReconnect());
    s.connected(T0 + milliseconds(1000));
    const auto out = s.receive(hello(), T0 + milliseconds(1000));
    ASSERT_EQ(out.size(), 1u);
    const Json resume = op(out[0]);
    EXPECT_EQ(resume["op"], 6);
    EXPECT_EQ(resume["d"]["session_id"], "SESSION");
    EXPECT_EQ(resume["d"]["seq"], 5);
}

TEST(GatewaySession, SessionTimeoutsIdentifyAgain)
{
    for (const int code : {4007, 4009}) {
        auto s = session();
        s.connected(T0);
        s.receive(hello(), T0);
        s.receive(ready(), T0);
        s.closed(code);
        EXPECT_EQ(s.connectUrl("wss://gateway.example"), "wss://gateway.example/?v=10&encoding=json") << code;
        s.connected(T0);
        EXPECT_EQ(op(s.receive(hello(), T0)[0])["op"], 2) << code;
    }
}

TEST(GatewaySession, RefusalsAreFatal)
{
    for (const int code : {4004, 4013, 4014}) {
        auto s = session();
        s.connected(T0);
        s.closed(code);
        EXPECT_TRUE(s.fatal()) << code;
    }
    auto s = session();
    s.connected(T0);
    s.closed(4000);
    EXPECT_FALSE(s.fatal());
}

TEST(GatewaySession, AnInvalidSessionWaitsThenIdentifies)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(), T0);
    s.receive(ready(), T0);
    s.receive(R"({"op":9,"d":false})", T0);
    EXPECT_TRUE(s.mustReconnect());
    EXPECT_GE(s.reconnectDelay(), milliseconds(1000));
    EXPECT_LE(s.reconnectDelay(), milliseconds(5000));
    s.connected(T0);
    EXPECT_EQ(op(s.receive(hello(), T0)[0])["op"], 2);
}

TEST(GatewaySession, ReconnectRequestsResume)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(), T0);
    s.receive(ready(), T0);
    s.receive(R"({"op":7})", T0);
    EXPECT_TRUE(s.mustReconnect());
    s.connected(T0);
    EXPECT_EQ(op(s.receive(hello(), T0)[0])["op"], 6);
}

TEST(GatewaySession, AConnectionThatNeverBecomesReadyIsGivenUp)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(40000), T0);
    s.receive(R"({"op":11})", T0 + milliseconds(20100));
    s.tick(T0 + milliseconds(20000));
    s.receive(R"({"op":11})", T0 + milliseconds(20100));
    s.tick(T0 + milliseconds(60000));
    s.receive(R"({"op":11})", T0 + milliseconds(60100));
    s.tick(T0 + milliseconds(100000));
    s.receive(R"({"op":11})", T0 + milliseconds(100100));
    EXPECT_FALSE(s.mustReconnect());
    s.tick(T0 + milliseconds(120000));
    EXPECT_TRUE(s.mustReconnect());
    EXPECT_NE(s.reconnectReason().find("READY"), std::string::npos);
}

TEST(GatewaySession, DispatchesBecomeEvents)
{
    auto s = session();
    s.connected(T0);
    s.receive(hello(), T0);
    s.receive(ready(), T0);
    s.receive(R"({"op":0,"s":2,"t":"MESSAGE_CREATE","d":{"content":"Afterglow#1"}})", T0);
    auto events = s.takeEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, "READY");
    EXPECT_EQ(events[1].type, "MESSAGE_CREATE");
    EXPECT_EQ(events[1].data["content"], "Afterglow#1");
    EXPECT_EQ(s.userId(), "42");
    EXPECT_TRUE(s.takeEvents().empty());
}

TEST(GatewaySession, GarbageIsIgnored)
{
    auto s = session();
    s.connected(T0);
    EXPECT_TRUE(s.receive("not json", T0).empty());
    EXPECT_TRUE(s.receive(R"({"op":"x"})", T0).empty());
    EXPECT_FALSE(s.mustReconnect());
}
