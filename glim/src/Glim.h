// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Config.h"
#include "AccessJwt.h"
#include "GatewaySession.h"
#include "HttpServer.h"
#include "Report.h"
#include "State.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Glim {

/// The bot: a mention in a post of a filed forum makes or updates the post's
/// GitHub issue; each round tells a post when its issue closes or opens again.
/// Nothing happens without a mention.
class Bot
{
public:
    Bot(Config config, HttpTransport discordTransport, HttpTransport githubTransport, GitHub::TokenSource githubToken);

    int run();
    void stop();
    void post(GatewayEvent event);

    // For the tests.
    void handle(const GatewayEvent& event);
    void round();
    const State& state() const
    {
        return m_state;
    }

    /// Checks a staff request's Access token: the email it is for, or nothing.
    using Verifier = std::function<std::optional<std::string>(const std::string& token, std::string* why)>;
    void setVerifier(Verifier verifier)
    {
        m_verify = std::move(verifier);
    }
    /// A request of the staff API (/staff/api/...), from its listener's
    /// thread; the action itself runs on the bot's.
    ServerResponse staffRequest(const ServerRequest& request);
    /// The JSON the issues page reads: api/posts.json and api/p/<post>.json.
    void publish();

private:
    void mentioned(const nlohmann::json& message);
    void file(const nlohmann::json& message, const nlohmann::json& thread, const std::string& words);
    std::optional<std::string> resolveRepo(std::string_view name);
    std::vector<std::string> tagNames(const nlohmann::json& thread);
    /// The post's messages after @p after, oldest first, without bots' and
    /// without the bare mentions of Glim; their text files downloaded.
    std::optional<std::vector<Report::Message>> collect(Snowflake thread, Snowflake after, Snowflake* newest);
    /// Downloads an attachment within @p budget (bytes left for this
    /// filing), strips its metadata and keeps it for the issue; a text file's
    /// bytes go in @p attachment for the issue too.
    void keep(Snowflake thread, Snowflake message, int index, Report::Attachment& attachment, std::size_t& budget);
    void reply(Snowflake channel, Snowflake to, const std::string& text);
    /// An embed in Glim's look; Discord's status of the post.
    long tell(Snowflake channel, std::optional<Snowflake> to, const std::string& title, const std::string& url,
              const std::string& description, int color);
    bool addTag(Snowflake thread, const std::string& tag);
    std::string withoutMention(std::string_view content) const;
    void save();
    /// The comment of what is new in a filed post since its last update;
    /// what happened, for the log or the page. @p requester 0: staff on the
    /// page; @p replyTo: the mention to answer, if any.
    std::string update(Snowflake channel, Snowflake requester, std::optional<Snowflake> replyTo);
    void keepFiles(PostRecord& record, const std::vector<Report::Message>& messages);
    ServerResponse staffAction(const ServerRequest& request, const std::string& email);
    /// Runs @p job on the bot's thread and waits for it (at once, before run()).
    ServerResponse onBotThread(std::function<ServerResponse()> job);
    std::int64_t now() const;

    Config m_config;
    Discord m_discord;
    HttpTransport m_fetch;
    GitHub m_github;
    State m_state;
    Snowflake m_user = 0;
    std::string m_avatar;
    std::map<std::string, std::string> m_knownRepos; ///< lowercase -> GitHub's spelling
    std::chrono::steady_clock::time_point m_reposFetched{};
    std::map<Snowflake, nlohmann::json> m_forums; ///< forum id -> channel, for its tags

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<GatewayEvent> m_events;
    std::string m_fatal;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::deque<std::function<void()>> m_jobs;
    std::unique_ptr<AccessJwt> m_access;
    Verifier m_verify;
    std::unique_ptr<HttpServer> m_server;
};

} // namespace Glim
