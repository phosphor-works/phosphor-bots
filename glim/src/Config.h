// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Glim {

/// The settings, from the environment (the service exports its env file). A
/// GLIM_ key that is not one of these stops the program.
struct Config
{
    std::string token; ///< GLIM_TOKEN: the Discord bot's token
    std::string appId; ///< GLIM_APP_ID: the GitHub App's id
    std::string appKeyPath; ///< GLIM_APP_KEY: the file of the app's private key (PEM)
    std::string org = "phosphor-works"; ///< GLIM_ORG
    std::set<Snowflake> forums; ///< GLIM_FORUMS: the forums whose posts it files
    /// GLIM_TAG_REPOS: a forum tag's name, lowercase, to its repository.
    std::map<std::string, std::string> tagRepos;
    /// GLIM_ALIASES: short names, lowercase, to a repository.
    std::map<std::string, std::string> aliases;
    /// GLIM_STAFF_ROLES: roles that may ask for any post, beside its author.
    std::set<Snowflake> staffRoles;
    std::string solvedTag = "solved"; ///< GLIM_SOLVED_TAG: put on a post whose issue is completed; empty: none
    std::vector<std::string> labels; ///< GLIM_LABELS: the labels of a new issue
    std::string statePath; ///< GLIM_STATE
    std::chrono::seconds pollInterval{300}; ///< GLIM_POLL_SECONDS: how often it looks at the issues
    std::size_t textLimit = 32 * 1024; ///< GLIM_TEXT_LIMIT: an attached log's bytes put in the issue
};

/// Reads @p env. Throws std::runtime_error naming the key and the mistake.
Config parseConfig(const std::map<std::string, std::string>& env);
std::map<std::string, std::string> processEnvironment();

} // namespace Glim
