// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Config.h"
#include "GatewaySession.h"
#include "Report.h"
#include "State.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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

private:
    void mentioned(const nlohmann::json& message);
    void file(const nlohmann::json& message, const nlohmann::json& thread, const std::string& words);
    std::optional<std::string> resolveRepo(std::string_view name);
    std::vector<std::string> tagNames(const nlohmann::json& thread);
    /// The post's messages after @p after, oldest first, without bots' and
    /// without the bare mentions of Glim; their text files downloaded.
    std::optional<std::vector<Report::Message>> collect(Snowflake thread, Snowflake after, Snowflake* newest);
    void reply(Snowflake channel, Snowflake to, const std::string& text);
    /// An embed in Glim's look; Discord's status of the post.
    long tell(Snowflake channel, std::optional<Snowflake> to, const std::string& title, const std::string& url,
              const std::string& description, int color);
    bool addTag(Snowflake thread, const std::string& tag);
    std::string withoutMention(std::string_view content) const;
    void save();

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
};

} // namespace Glim
