// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Config.h"
#include "References.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Wisp {

/// A message Wisp keeps up to date: a card, or a push's or a release's.
struct CardRecord
{
    Snowflake channel = 0;
    Snowflake message = 0;
    std::string drawn; ///< a hash of the message as last drawn
    std::string activity; ///< what moves the card to the end when it changes
    std::int64_t postedAt = 0; ///< Unix seconds
    std::int64_t closedAt = 0; ///< Unix seconds when first seen closed; 0 while open
    bool operator==(const CardRecord&) const = default;
};

/// A reply to a message that named issues or pull requests.
struct ReplyRecord
{
    Snowflake channel = 0;
    Snowflake message = 0; ///< the reply's
    Snowflake source = 0; ///< the message it answers
    std::vector<Reference> references;
    std::string drawn;
    std::int64_t at = 0;
    bool operator==(const ReplyRecord&) const = default;
};

/// What the feed knows of a repository: its default branch's head, the
/// releases already posted.
struct RepoRecord
{
    std::string head;
    std::vector<long long> releases;
    bool operator==(const RepoRecord&) const = default;
};

/// What Wisp keeps between runs: message ids and hashes, and of people only
/// the latest comment each card shows (its author, link and first words), as
/// GitHub shows them publicly. No Discord message's text, no Discord author.
struct State
{
    std::map<std::string, CardRecord>
        cards; ///< by key: "pr:Repo#1", "issue:Repo#2", "push:Repo:SHA", "release:Repo:ID"
    std::vector<ReplyRecord> replies;
    std::map<std::string, RepoRecord> repos;
    std::map<std::string, Comment> latestComments; ///< by "Repo#N"
    bool operator==(const State&) const = default;
};

nlohmann::json toJson(const State& state);
State stateFromJson(const nlohmann::json& json);

/// The state in @p path; nothing when it is missing or cannot be read (said
/// in the log), so that the first round starts clean.
std::optional<State> loadState(const std::string& path);
/// Writes @p state to @p path through a new file and a rename, so that a
/// crash leaves the old one whole. False (said in the log) on failure.
bool saveState(const std::string& path, const State& state);

} // namespace Wisp
