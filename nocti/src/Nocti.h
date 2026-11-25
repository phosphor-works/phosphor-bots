// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Config.h"
#include "Direct.h"
#include "Gateway.h"
#include "GatewaySession.h"
#include "McpServer.h"
#include "People.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace Nocti {

/// A post the session wrote, waiting in the owner's DMs for his check mark.
struct Draft
{
    Snowflake id = 0; ///< the draft's message, in the owner's DMs
    Snowflake dm = 0;
    Snowflake channel = 0;
    Snowflake replyTo = 0;
    std::string text;
    std::string status = "waiting"; ///< waiting, sent, not sent
    Snowflake posted = 0;
    std::string link;
};

/// Nocti: a Claude Code session on Discord as a bot of its own. The session
/// reads and waits through its tools, and posts only through a draft its owner
/// approves with a check mark in his DMs. Of what Discord brings, only the
/// owner's messages and those of the people he trusts or allowed reach the
/// session, within their hourly weight and past the pre-harness; a message
/// addressed to Nocti wakes the session as a channel event.
class Bot
{
public:
    Bot(Config config, HttpTransport transport, McpServer& mcp, People::Now now = {});
    ~Bot();

    /// The tools, on @p mcp.
    void registerTools();
    /// Connects to the gateway (once the session is initialized).
    void start();
    void stop();
    /// Another session holds Nocti: every tool says so, and no gateway runs.
    bool passive() const
    {
        return m_passive;
    }

    /// A dispatch of the gateway (from its thread, or the tests').
    void handle(const GatewayEvent& event);

    // The tools, for the tests.
    nlohmann::json read(const nlohmann::json& arguments);
    nlohmann::json wait(const nlohmann::json& arguments);
    nlohmann::json post(const nlohmann::json& arguments);
    nlohmann::json postStatus(const nlohmann::json& arguments);
    nlohmann::json react(const nlohmann::json& arguments, bool add);
    nlohmann::json attachments(const nlohmann::json& arguments);
    nlohmann::json allowed(const nlohmann::json& arguments);

    /// The instructions Claude Code gives the session.
    static std::string instructions(const Config& config);
    /// The emojis Nocti reacts with.
    static const std::set<std::string>& reactions();

    /// How often a waiting tool looks again (the tests shorten it).
    std::chrono::milliseconds pollInterval{1000};

private:
    struct Channel
    {
        std::string name;
        Snowflake guild = 0;
        Snowflake parent = 0;
        int type = 0;
    };
    std::optional<Channel> channelInfo(Snowflake id);
    bool mayUse(Snowflake channel);
    Snowflake ownerDm();
    DiscordResult rest(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    /// What the session may see of @p message: nothing for a stranger's.
    std::optional<nlohmann::json> visible(const nlohmann::json& message);
    void message(const nlohmann::json& data);
    void reaction(const nlohmann::json& data);
    void interaction(const nlohmann::json& data);
    void registerCommands(Snowflake guild);
    void decide(Snowflake draft, bool approved);
    void checkDraft(Snowflake draft);
    nlohmann::json draftAnswer(const Draft& draft) const;
    /// What a reply to @p message in @p channel would answer, for the policy.
    std::optional<Target> target(Snowflake channel, Snowflake message);
    nlohmann::json postDirect(Snowflake channel, Snowflake replyTo, const std::string& text);
    void notice(const std::string& text, std::map<std::string, std::string> meta);

    Config m_config;
    Discord m_discord;
    HttpTransport m_fetch;
    McpServer& m_mcp;
    People::Now m_now;
    bool m_passive = false;
    int m_lockFd = -1;

    std::mutex m_restMutex; ///< Discord's client is for one thread at a time
    std::recursive_mutex m_mutex; ///< the rest of the state
    std::condition_variable_any m_changed;
    People m_people;
    DirectPolicy m_direct;
    Snowflake m_me = 0;
    Snowflake m_application = 0;
    Snowflake m_dm = 0;
    std::map<Snowflake, Channel> m_channels;
    std::map<Snowflake, Draft> m_drafts;
    std::deque<nlohmann::json> m_recent; ///< the last messages that reached the session, for wait
    std::set<Snowflake> m_ownMessages;
    std::set<Snowflake> m_strangerDms; ///< told the session once each
    std::set<Snowflake> m_registered;
    std::unique_ptr<Gateway> m_gateway;
};

} // namespace Nocti
