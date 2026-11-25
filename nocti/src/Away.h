// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nocti {

/// Where the owner is, which sets how a post goes that only his check mark
/// lets out, or that the interval holds back.
///
/// | Mode | A post without direct | A direct post the interval holds back |
/// | --- | --- | --- |
/// | home (the default) | a draft | an error: the session sends a draft |
/// | work | held until he is home (the approver's draft at once, if one is set) | queued for its turn |
/// | asleep | as work; once he is home, what was held comes as silent drafts and one summary | queued |
enum class Mode {
    Home,
    Work,
    Asleep
};

std::string modeName(Mode mode);
std::optional<Mode> parseMode(std::string_view name);
/// What a mode does, as the owner's answer and the session's notice say it.
std::string modeMeans(Mode mode);
/// A time as 30m, 8h or 1d, from 1 minute to 7 days, in seconds.
std::optional<std::int64_t> parseSpan(std::string_view text);

/// A post that waits: queued for the interval's turn, or held until the owner is home.
struct Waiting
{
    std::string mark; ///< what discord_cancel takes it back by
    std::int64_t since = 0;
    Mode mode = Mode::Home; ///< the mode it was held in
    Snowflake channel = 0;
    Snowflake replyTo = 0;
    std::string text;
};

/// The mode, the approver, and the posts that wait, kept in away.json.
class Away
{
public:
    using Now = std::function<std::int64_t()>;
    static constexpr int AwayMinutes = 10; ///< the interval away from home
    static constexpr std::int64_t MostSpan = 7 * 86400;

    explicit Away(std::string path, Now now = {});

    /// The mode in force: home once a mode set for a time ran out.
    Mode mode();
    bool away()
    {
        return mode() != Mode::Home;
    }
    /// When the mode ends; 0 for at the next change.
    std::int64_t modeUntil();
    /// Sets the mode for @p seconds (0: until the next change); returns the one it replaced.
    Mode setMode(Mode mode, std::int64_t seconds);
    /// The mode that ran out since the last call, once.
    std::optional<Mode> ranOut();

    /// The approver while their time runs; 0 for none.
    Snowflake approver();
    std::int64_t approverUntil();
    void setApprover(Snowflake user, std::int64_t seconds);
    /// False when there was none.
    bool clearApprover();
    /// The approver whose time ran out since the last call, once.
    std::optional<Snowflake> approverRanOut();

    /// Keeps a post until the owner is home; returns its mark.
    std::string hold(Snowflake channel, Snowflake replyTo, const std::string& text);
    /// Queues a direct post for the interval's turn; returns its mark.
    std::string queue(Snowflake channel, Snowflake replyTo, const std::string& text);
    const std::deque<Waiting>& queued() const
    {
        return m_queue;
    }
    std::vector<Waiting> held() const
    {
        return m_held;
    }
    /// Takes the first queued post off the queue, noting how it left.
    std::optional<Waiting> takeQueued(const std::string& how);
    /// Takes every held post, noting how they left.
    std::vector<Waiting> takeHeld(const std::string& how);
    /// Notes how a post that left went after all ("failed").
    void noteGone(const std::string& mark, const std::string& how);
    /// Takes a waiting post back: empty when it was; else why not ("posted", "a draft", "no such post").
    std::string cancel(const std::string& mark);

private:
    std::string nextMark(char kind);
    void prune();
    void load();
    void save();

    std::string m_path;
    Now m_now;
    Mode m_mode = Mode::Home;
    std::int64_t m_since = 0;
    std::int64_t m_until = 0;
    std::optional<Mode> m_ranOut;
    Snowflake m_approver = 0;
    std::int64_t m_approverUntil = 0;
    std::optional<Snowflake> m_approverRanOut;
    std::deque<Waiting> m_queue;
    std::vector<Waiting> m_held;
    std::map<std::string, std::pair<std::string, std::int64_t>> m_gone; ///< mark -> how it left, when (a day)
    std::int64_t m_nextMark = 1;
};

} // namespace Nocti
