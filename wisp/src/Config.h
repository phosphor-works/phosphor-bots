// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Wisp {

/// The settings, from the environment (the service exports its env file).
/// A WISP_ key that is not one of these stops the program, so a typo cannot
/// leave a feature off without a word.
struct Config
{
    std::string token; ///< WISP_TOKEN: the Discord bot's token
    std::string githubToken; ///< GITHUB_TOKEN: read-only, public repositories
    std::string org = "phosphor-works"; ///< WISP_ORG
    /// WISP_REPOS: the repositories it follows; empty: every public,
    /// unarchived repository of the organization.
    std::vector<std::string> repos;
    /// WISP_ALIASES: short names, lowercase, to a repository's name
    /// (`pz=PlasmaZones`).
    std::map<std::string, std::string> aliases;
    /// WISP_DEFAULT_REPO: the repository of a bare `#123`; empty: a bare
    /// number names nothing, unless its channel has a repository.
    std::string defaultRepo;
    /// WISP_CHANNEL_REPOS: a channel's own repository for a bare `#123`.
    std::map<Snowflake, std::string> channelRepos;
    /// WISP_CHANNELS: the channels it replies in; empty: every channel.
    std::set<Snowflake> channels;
    std::optional<Snowflake> cardsPulls; ///< WISP_CARDS_PRS
    std::optional<Snowflake> cardsIssues; ///< WISP_CARDS_ISSUES
    std::optional<Snowflake> feed; ///< WISP_FEED: pushes to the default branch and releases
    std::string statePath; ///< WISP_STATE: empty keeps nothing between runs
    std::string iconUrl; ///< WISP_ICON_URL: Wisp's avatar in each card's footer (HTTPS)
    std::chrono::seconds pollInterval{30}; ///< WISP_POLL_SECONDS
    std::chrono::hours closedKeep{24}; ///< WISP_CLOSED_HOURS
    std::chrono::hours replyFollow{24 * 7}; ///< WISP_REPLY_DAYS (in days)
    std::size_t maxReferences = 5; ///< WISP_MAX_REFERENCES, 1 to 10

    bool cardsWanted() const
    {
        return cardsPulls || cardsIssues || feed;
    }
};

/// Reads @p env (NAME=VALUE pairs). Throws std::runtime_error naming the key
/// and what is wrong with it.
Config parseConfig(const std::map<std::string, std::string>& env);

/// The process's environment as NAME=VALUE pairs.
std::map<std::string, std::string> processEnvironment();

} // namespace Wisp
