// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Away.h"
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
    Snowflake copyDm = 0; ///< the approver's DM with Nocti, when a copy went
    Snowflake copy = 0; ///< the copy in the approver's DMs
    Snowflake approver = 0; ///< who got the copy
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
    nlohmann::json freeze(const nlohmann::json& arguments);
    nlohmann::json cancel(const nlohmann::json& arguments);

    /// Each few seconds: a mode or an approver that ran out, and the queue's turn.
    void tick();
    /// A permission prompt of Claude Code (NOCTI_RELAY): a card in the owner's DMs.
    void permissionRequest(const nlohmann::json& params);
    /// The owner's status line: "nocti: work until 18:00, approves Sam until 26/11 14:07".
    std::string statusLine();

    /// The fixed line of an ice of @p kind (harness, owner-call, secret, abuse,
    /// injection), its pings with it; empty for a kind it does not know.
    std::string iceLine(const std::string& kind, Snowflake guild);
    /// The fixed greeting of a stranger who calls Nocti.
    std::string greeting() const;

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
    void decide(Snowflake draft, bool approved, Snowflake by);
    void checkDraft(Snowflake draft);
    nlohmann::json draftAnswer(const Draft& draft) const;
    /// What a reply to @p message in @p channel would answer, for the policy.
    std::optional<Target> target(Snowflake channel, Snowflake message);
    nlohmann::json postDirect(Snowflake channel, Snowflake replyTo, const std::string& text);
    void notice(const std::string& text, std::map<std::string, std::string> meta);
    void greet(const nlohmann::json& message);
    /// Posts an ice's line in reply to @p message and ices its author.
    nlohmann::json ice(Snowflake channel, Snowflake guild, Snowflake message, Snowflake author,
                       const std::string& kind);
    struct Made
    {
        Snowflake draft = 0;
        std::string failure;
    };
    /// A draft in the owner's DMs, and the approver's copy while there is one.
    Made makeDraft(Snowflake channel, Snowflake replyTo, const std::string& text, bool silent);
    Snowflake dmWith(Snowflake user);
    void syncInterval();
    void postQueued(const Waiting& waiting);
    /// Makes drafts of the posts that waited for the owner to be home.
    void releaseHeld(std::vector<Waiting> held);
    /// Hands the owner's answer to a permission prompt to Claude Code; false for one it does not know.
    bool verdict(const std::string& request, bool allow);
    static std::string toneMeans(const std::string& tone);
    std::string toneOf(Snowflake channel, const std::optional<Channel>& info, const std::string& text);
    /// The state folder's line file, when what it says changed or each 5 minutes.
    void writeLine(bool force);
    void loadExtras();
    void saveExtras();

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
    Away m_away;
    Snowflake m_me = 0;
    Snowflake m_application = 0;
    Snowflake m_dm = 0;
    std::map<Snowflake, Channel> m_channels;
    std::map<Snowflake, Draft> m_drafts;
    std::deque<nlohmann::json> m_recent; ///< the last messages that reached the session, for wait
    std::set<Snowflake> m_ownMessages;
    std::set<Snowflake> m_strangerDms; ///< told the session once each
    std::set<Snowflake> m_registered;
    std::map<Snowflake, std::int64_t> m_greeted; ///< a stranger's last greeting
    std::deque<std::int64_t> m_greetings; ///< everyone's, of the last hour
    std::map<Snowflake, Snowflake> m_admins; ///< server -> its admin role
    std::map<Snowflake, Snowflake> m_copies; ///< an approver's copy -> its draft
    std::map<Snowflake, Snowflake> m_dms; ///< a user -> Nocti's DM with them
    std::map<std::string, Snowflake> m_permissions; ///< an open permission prompt -> its card
    std::map<Snowflake, std::string> m_permissionCards; ///< a card -> its prompt
    std::map<Snowflake, std::string> m_tones; ///< a channel -> playful or serious
    std::string m_line;
    std::int64_t m_lineWritten = 0;
    std::thread m_ticker;
    bool m_stopTicker = false;
    std::unique_ptr<Gateway> m_gateway;
};

} // namespace Nocti
