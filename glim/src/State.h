// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Report.h"

#include <cstdint>
#include <map>
#include <vector>
#include <optional>
#include <string>

namespace Glim {

/// A post Glim filed: its issue, the last message the issue holds, and who is
/// who in it (Discord ids, which leave the server only as "Member N").
/// A file kept for an issue, as the issues page lists it.
struct KeptFile
{
    std::string name; ///< as attached (shown, as on GitHub)
    std::string kind; ///< image, video, text, file
    long long size = 0;
    std::string url;
    std::string path; ///< under GLIM_FILES_DIR: "<post>/<message>-<n>.<extension>"
    bool removed = false; ///< taken down by staff
    bool operator==(const KeptFile&) const = default;
};

struct PostRecord
{
    std::string repo;
    int number = 0;
    std::string url;
    Snowflake lastMessage = 0;
    Report::Participants participants;
    std::string issueState = "open"; ///< as last told in the post
    std::string title;
    std::int64_t filedAt = 0; ///< Unix seconds
    std::int64_t updatedAt = 0; ///< the last filing or update
    std::string discordUrl;
    std::vector<KeptFile> files;
    bool operator==(const PostRecord& other) const
    {
        return repo == other.repo && number == other.number && url == other.url && lastMessage == other.lastMessage
            && participants.reporter == other.participants.reporter
            && participants.members == other.participants.members && issueState == other.issueState
            && title == other.title && filedAt == other.filedAt && updatedAt == other.updatedAt
            && discordUrl == other.discordUrl && files == other.files;
    }
};

struct State
{
    std::map<Snowflake, PostRecord> posts; ///< by the post's (thread's) id
    bool operator==(const State&) const = default;
};

nlohmann::json toJson(const State& state);
/// A post's files as JSON: for the state, or for the public (no paths, and
/// none taken down).
nlohmann::json filesJson(const std::vector<KeptFile>& files, bool forPublic = false);
State stateFromJson(const nlohmann::json& json);
std::optional<State> loadState(const std::string& path);
bool saveState(const std::string& path, const State& state);

} // namespace Glim
