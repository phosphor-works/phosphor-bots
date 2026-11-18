// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Config.h"
#include "Embeds.h"
#include "State.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace Wisp {

/// The cards: a message for each open pull request and each open issue of
/// the followed repositories, in their channels, and the feed's message for
/// each push to a default branch and each release. Each round draws them from
/// what GitHub says now:
///
/// - a card whose drawing changed is edited;
/// - activity (a push, a person's comment, a change of state, a finished
///   run of CI) posts it again at the end of its channel and deletes the old;
/// - a closed item's card stays closedKeep, as it closed, then goes;
/// - a card that cannot be drawn whole (GitHub did not answer) waits, as it
///   was, for the next round.
class Cards
{
public:
    using Now = std::function<std::int64_t()>; ///< Unix seconds

    Cards(const Config& config, GitHub& github, Discord& discord, State& state, Now now = {});

    /// One round over every followed repository.
    void round();
    /// Deletes the messages of @p botUser in the pull requests' and the
    /// issues' channels that no card keeps: those of a state that was lost.
    void cleanUp(Snowflake botUser);
    /// The repositories followed, as of the last round.
    const std::vector<Repository>& repositories() const
    {
        return m_repositories;
    }
    /// Fetches the organization's repositories (the followed ones). False when
    /// GitHub did not answer.
    bool refreshRepositories();

    void setLook(Embeds::Look look)
    {
        m_look = std::move(look);
    }
    const Embeds::Look& look() const
    {
        return m_look;
    }

    /// The stable hash of a message, for CardRecord::drawn (FNV-1a, 64 bits).
    static std::string hash(const nlohmann::json& message);

private:
    void repoCards(const Repository& repo);
    void repoFeed(const Repository& repo);
    void drawItem(const Item& item, Snowflake channel, std::int64_t closedAt);
    std::optional<Embeds::Push> push(const Repository& repo, const std::string& before, const std::string& head);
    void sync(const std::string& key, Snowflake channel, const nlohmann::json& message, const std::string& activity);
    void retire(const std::string& key);

    const Config& m_config;
    GitHub& m_github;
    Discord& m_discord;
    State& m_state;
    Now m_now;
    std::vector<Repository> m_repositories;
    Embeds::Look m_look;
};

} // namespace Wisp
