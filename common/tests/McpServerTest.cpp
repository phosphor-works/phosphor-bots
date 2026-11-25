// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "McpServer.h"

#include <gtest/gtest.h>

#include <sstream>

using namespace Bots;
using Json = nlohmann::json;

namespace {

struct Fixture
{
    std::istringstream in;
    std::ostringstream out;
    McpServer server{{"nocti", "1.0", "Read the events.", true, {"claude-code"}}, in, out};
    bool initialized = false;

    Fixture()
    {
        server.onInitialized = [this] {
            initialized = true;
        };
        server.addTool({"echo", "Says it back", {{"type", "object"}}, [](const Json& arguments) -> Json {
                            if (arguments.value("text", "") == "fail")
                                throw ToolError("it said fail");
                            return arguments.value("text", "");
                        }});
    }

    std::vector<Json> lines()
    {
        std::vector<Json> out_;
        std::istringstream text(out.str());
        for (std::string line; std::getline(text, line);)
            out_.push_back(Json::parse(line));
        return out_;
    }
};

std::string initialize(const std::string& client, const std::string& version = "2025-06-18")
{
    return Json{{"jsonrpc", "2.0"},
                {"id", 1},
                {"method", "initialize"},
                {"params", {{"protocolVersion", version}, {"clientInfo", {{"name", client}, {"version", "2"}}}}}}
        .dump();
}

} // namespace

TEST(McpServer, InitializeDeclaresTheChannel)
{
    Fixture f;
    f.server.handleLine(initialize("claude-code"));
    const Json answer = f.lines().at(0);
    EXPECT_EQ(answer["id"], 1);
    EXPECT_EQ(answer["result"]["protocolVersion"], "2025-06-18");
    EXPECT_TRUE(answer["result"]["capabilities"]["experimental"].contains("claude/channel"));
    EXPECT_TRUE(answer["result"]["capabilities"].contains("tools"));
    EXPECT_EQ(answer["result"]["instructions"], "Read the events.");
    EXPECT_EQ(answer["result"]["serverInfo"]["name"], "nocti");
}

TEST(McpServer, AnUnknownRevisionGetsTheNewest)
{
    Fixture f;
    f.server.handleLine(initialize("claude-code", "2026-07-28"));
    EXPECT_EQ(f.lines().at(0)["result"]["protocolVersion"], "2025-11-25");
}

TEST(McpServer, AnotherClientIsRefused)
{
    Fixture f;
    f.server.handleLine(initialize("some-other-agent"));
    const Json answer = f.lines().at(0);
    EXPECT_TRUE(answer.contains("error"));
    EXPECT_NE(std::string(answer["error"]["message"]).find("claude-code alone"), std::string::npos);
}

TEST(McpServer, InitializedIsCalledOnce)
{
    Fixture f;
    f.server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    EXPECT_TRUE(f.initialized);
    f.initialized = false;
    f.server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    EXPECT_FALSE(f.initialized);
    EXPECT_TRUE(f.lines().empty()) << "a notification has no answer";
}

TEST(McpServer, ToolsAreListedAndCalled)
{
    Fixture f;
    f.server.handleLine(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    f.server.handleLine(
        R"({"jsonrpc":"2.0","id":"a","method":"tools/call","params":{"name":"echo","arguments":{"text":"hi"}}})");
    f.server.handleLine(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"text":"fail"}}})");
    f.server.handleLine(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"nope"}})");
    f.server.joinCalls();
    std::map<std::string, Json> byId;
    for (const auto& line : f.lines())
        byId[line["id"].dump()] = line;
    EXPECT_EQ(byId["2"]["result"]["tools"][0]["name"], "echo");
    EXPECT_EQ(byId["\"a\""]["result"]["content"][0]["text"], "hi");
    EXPECT_FALSE(byId["\"a\""]["result"].contains("isError"));
    EXPECT_EQ(byId["3"]["result"]["isError"], true);
    EXPECT_EQ(byId["3"]["result"]["content"][0]["text"], "it said fail");
    EXPECT_EQ(byId["4"]["error"]["code"], -32602);
}

TEST(McpServer, UnknownMethodsAndGarbage)
{
    Fixture f;
    f.server.handleLine(R"({"jsonrpc":"2.0","id":5,"method":"server/discover"})");
    f.server.handleLine("not json");
    f.server.handleLine(R"({"jsonrpc":"2.0","id":6,"method":"ping"})");
    const auto lines = f.lines();
    EXPECT_EQ(lines.at(0)["error"]["code"], -32601);
    EXPECT_EQ(lines.at(1)["error"]["code"], -32700);
    EXPECT_EQ(lines.at(2)["result"], Json::object());
}

TEST(McpServer, ChannelEventsKeepOnlyIdentifierKeys)
{
    Fixture f;
    f.server.channel("hello", {{"kind", "message"}, {"author_id", "42"}, {"bad-key", "x"}, {"empty", ""}});
    const Json event = f.lines().at(0);
    EXPECT_EQ(event["method"], "notifications/claude/channel");
    EXPECT_EQ(event["params"]["content"], "hello");
    EXPECT_EQ(event["params"]["meta"], (Json{{"kind", "message"}, {"author_id", "42"}}));
}

TEST(McpServer, RunServesUntilTheInputEnds)
{
    std::istringstream in(initialize("claude-code") + "\n" + R"({"jsonrpc":"2.0","id":2,"method":"ping"})" + "\r\n");
    std::ostringstream out;
    McpServer server({"x", "1", "", false, {}}, in, out);
    server.run();
    EXPECT_TRUE(server.stopping());
    const std::string text = out.str();
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 2);
    EXPECT_FALSE(Json::parse(text.substr(0, text.find('\n')))["result"]["capabilities"].contains("experimental"));
}
