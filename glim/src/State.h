// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "Report.h"

#include <map>
#include <optional>
#include <string>

namespace Glim {

/// A post Glim filed: its issue, the last message the issue holds, and who is
/// who in it (Discord ids, which leave the server only as "Member N").
struct PostRecord
{
    std::string repo;
    int number = 0;
    std::string url;
    Snowflake lastMessage = 0;
    Report::Participants participants;
    std::string issueState = "open"; ///< as last told in the post
    bool operator==(const PostRecord& other) const
    {
        return repo == other.repo && number == other.number && url == other.url && lastMessage == other.lastMessage
            && participants.reporter == other.participants.reporter
            && participants.members == other.participants.members && issueState == other.issueState;
    }
};

struct State
{
    std::map<Snowflake, PostRecord> posts; ///< by the post's (thread's) id
    bool operator==(const State&) const = default;
};

nlohmann::json toJson(const State& state);
State stateFromJson(const nlohmann::json& json);
std::optional<State> loadState(const std::string& path);
bool saveState(const std::string& path, const State& state);

} // namespace Glim
