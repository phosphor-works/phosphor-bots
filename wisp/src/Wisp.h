// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Cards.h"
#include "Common.h"
#include "Config.h"
#include "GatewaySession.h"
#include "References.h"
#include "State.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace Wisp {

/// The bot: the gateway's messages answered with links, and the cards drawn
/// each round, on one thread (the gateway keeps its own).
class Bot
{
public:
    Bot(Config config, HttpTransport discordTransport, HttpTransport githubTransport);

    /// Runs until stop() or until Discord refuses the bot. The exit status:
    /// 0 after stop(), 1 when refused.
    int run();
    void stop();

    /// A dispatch of the gateway, from its thread.
    void post(GatewayEvent event);

    // For the tests: one event handled, one round, the state.
    void handle(const GatewayEvent& event);
    void cardsRound();
    void refreshReplies();
    const State& state() const
    {
        return m_state;
    }
    /// The repository's own name for a name or alias someone wrote.
    std::optional<std::string> resolveRepo(std::string_view name) const;

private:
    void message(const nlohmann::json& data);
    void messageDeleted(const nlohmann::json& data);
    void refreshKnownRepos();
    void save();
    std::vector<Item> lookUp(const std::vector<Reference>& references);
    std::int64_t now() const;

    Config m_config;
    Discord m_discord;
    GitHub m_github;
    State m_state;
    bool m_stateLoaded = false;
    Cards m_cards;
    std::map<std::string, std::string> m_knownRepos; ///< lowercase -> GitHub's spelling
    Snowflake m_botUser = 0;
    bool m_cleaned = false;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<GatewayEvent> m_events;
    std::string m_fatal;
    std::atomic<bool> m_stop{false};
};

} // namespace Wisp
