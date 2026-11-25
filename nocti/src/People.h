// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Nocti {

enum class Level {
    Owner, ///< everything reaches the session
    Trusted, ///< 30 messages an hour
    Allowed, ///< 10 messages an hour, until the allow runs out
    Stranger, ///< nothing reaches the session: counted only
};

/// An ice: someone whose messages are only counted for a while.
struct Ice
{
    std::int64_t until = 0;
    std::vector<std::int64_t> strikes; ///< each ice of the last 7 days
};

struct Allow
{
    std::string name; ///< as the server showed them when allowed
    std::int64_t since = 0;
    std::int64_t until = 0; ///< Unix seconds; 0: for good
};

/// Who may reach the session and how much: the owner, the people the owner
/// trusts (NOCTI_TRUSTED, then /nocti trust and untrust) and allows (for a
/// time), each within an hourly weight. Kept in people.json under the state
/// folder (mode 600), so a restart keeps it.
class People
{
public:
    using Now = std::function<std::int64_t()>;

    People(Snowflake owner, std::set<Snowflake> trustedAtStart, std::string path, Now now = {});

    Level level(Snowflake user);
    /// Counts one message of @p user: whether it is within their hourly weight.
    bool take(Snowflake user);
    /// "owner", "trusted", "allowed until 2026-11-26 14:00 UTC", "stranger".
    std::string describe(Snowflake user);
    /// "3 of 30 this hour" for someone weighed; empty for the owner and strangers.
    std::string heard(Snowflake user);

    void trust(Snowflake user);
    void untrust(Snowflake user);
    /// @p seconds 0: for good. Returns the allow it replaced, if any.
    std::optional<Allow> allow(Snowflake user, const std::string& name, std::int64_t seconds);
    bool deny(Snowflake user);
    std::map<Snowflake, Allow> allowed();
    std::set<Snowflake> trusted() const;

    /// Ices @p user: 2 hours, or 1 day when they were iced in the last 7 days.
    /// Returns its end. Never the owner nor someone he trusts (nothing then).
    std::optional<std::int64_t> freeze(Snowflake user);
    bool unfreeze(Snowflake user);
    /// Their ice's end while it holds.
    std::optional<std::int64_t> iced(Snowflake user);
    /// The ices of the last 7 days.
    int strikes(Snowflake user);

    static int hourlyWeight(Level level);

private:
    void load();
    void save() const;
    void expire();

    Snowflake m_owner;
    std::set<Snowflake> m_atStart;
    std::set<Snowflake> m_trusted;
    std::set<Snowflake> m_untrusted; ///< of NOCTI_TRUSTED, untrusted since
    std::map<Snowflake, Allow> m_allowed;
    std::map<Snowflake, std::deque<std::int64_t>> m_heard;
    std::map<Snowflake, Ice> m_ice;
    std::string m_path;
    Now m_now;
};

/// A time as "2026-11-26 14:00 UTC".
std::string utcTime(std::int64_t seconds);

} // namespace Nocti
