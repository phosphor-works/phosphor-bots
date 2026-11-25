// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"
#include "People.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace Nocti {

/// The message a direct post would answer, as the policy weighs it.
struct Target
{
    Snowflake message = 0;
    Snowflake author = 0;
    Level level = Level::Stranger;
    bool own = false; ///< one of Nocti's own messages
    bool caution = false; ///< the pre-harness gave it a caution
    bool dm = false; ///< in a DM channel
    bool ownerDm = false; ///< in the owner's DM with Nocti
};

/// When a post may go out with no draft:
///
/// | It answers | It goes direct |
/// | --- | --- |
/// | the owner | up to 3 replies to that message (in DMs, only in his) |
/// | someone he trusts | up to 3 replies to that message |
/// | someone he allowed, or nothing (a post of its own), or Nocti's own message | once an interval |
/// | a stranger, or a message with a caution | never: a draft |
///
/// What it has done is kept in direct.json (the last day), so a restart
/// keeps the counts.
class DirectPolicy
{
public:
    using Now = std::function<std::int64_t()>;
    static constexpr int RepliesPerMessage = 3;

    DirectPolicy(std::chrono::minutes interval, std::string path, Now now = {});

    /// Why @p target may not be answered directly now; empty: it may.
    std::string refusal(const std::optional<Target>& target);
    /// Counts a direct post that went out.
    void record(const std::optional<Target>& target);
    /// How an answer to @p target goes, for the session: "direct, 3 left",
    /// "direct from 14:30 UTC (interval 30 min)", "a draft only: a stranger's message".
    std::string describe(const std::optional<Target>& target);
    /// The direct posts left for @p message, or -1 when it takes the interval.
    int left(const std::optional<Target>& target);

private:
    bool needsInterval(const std::optional<Target>& target) const;
    void load();
    void save() const;
    void prune();

    std::chrono::minutes m_interval;
    std::string m_path;
    Now m_now;
    std::map<Snowflake, std::vector<std::int64_t>> m_replies; ///< message -> when each reply to it went
    std::int64_t m_lastInterval = 0;
};

} // namespace Nocti
