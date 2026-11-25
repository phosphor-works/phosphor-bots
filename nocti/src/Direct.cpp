// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Direct.h"

#include "Files.h"

#include <format>

namespace Nocti {

using Json = nlohmann::json;

namespace {

constexpr std::int64_t Day = 86400;

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

DirectPolicy::DirectPolicy(std::chrono::minutes interval, std::string path, Now now)
    : m_interval(interval)
    , m_path(std::move(path))
    , m_now(now ? std::move(now) : Now(systemNow))
{
    load();
}

bool DirectPolicy::needsInterval(const std::optional<Target>& target) const
{
    return !target || target->own || target->level == Level::Allowed;
}

std::string DirectPolicy::refusal(const std::optional<Target>& target)
{
    prune();
    if (target && !target->own) {
        if (target->level == Level::Stranger)
            return "a stranger's message: a draft only";
        if (target->caution && target->level != Level::Owner)
            return "the message hit the pre-harness (a caution): a draft only";
        if (target->dm && !target->ownerDm)
            return "a DM other than the owner's: a draft only";
    }
    if (needsInterval(target)) {
        const std::int64_t next = m_lastInterval + std::chrono::seconds(m_interval).count();
        if (m_lastInterval && m_now() < next)
            return std::format("the interval: the next post of its own may go at {} ({} min)", utcTime(next),
                               m_interval.count());
        return {};
    }
    if (left(target) <= 0)
        return std::format("{} replies to that message went already: a draft only", RepliesPerMessage);
    return {};
}

int DirectPolicy::left(const std::optional<Target>& target)
{
    if (needsInterval(target))
        return -1;
    prune();
    const auto it = m_replies.find(target->message);
    return RepliesPerMessage - int(it == m_replies.end() ? 0 : it->second.size());
}

void DirectPolicy::record(const std::optional<Target>& target)
{
    if (needsInterval(target))
        m_lastInterval = m_now();
    else
        m_replies[target->message].push_back(m_now());
    save();
}

std::string DirectPolicy::describe(const std::optional<Target>& target)
{
    const std::string refused = refusal(target);
    if (refused.empty()) {
        const int replies = left(target);
        return replies < 0 ? std::format("direct now (then once each {} min)", m_interval.count())
                           : std::format("direct, {} left", replies);
    }
    if (refused.starts_with("the interval"))
        return "direct from " + utcTime(m_lastInterval + std::chrono::seconds(m_interval).count())
            + std::format(" (interval {} min); a draft before", m_interval.count());
    return refused;
}

void DirectPolicy::prune()
{
    const std::int64_t cutoff = m_now() - Day;
    for (auto it = m_replies.begin(); it != m_replies.end();) {
        std::erase_if(it->second, [&](std::int64_t when) {
            return when <= cutoff;
        });
        it = it->second.empty() ? m_replies.erase(it) : std::next(it);
    }
}

void DirectPolicy::load()
{
    if (m_path.empty())
        return;
    const auto text = readFile(m_path);
    if (!text)
        return;
    const Json json = Json::parse(*text, nullptr, false);
    if (!json.is_object())
        return;
    m_lastInterval = json.value("last_interval", std::int64_t(0));
    if (json.contains("replies") && json["replies"].is_object()) {
        for (const auto& [id, times] : json["replies"].items()) {
            if (const auto message = parseSnowflake(id); message && times.is_array())
                for (const auto& when : times)
                    if (when.is_number_integer())
                        m_replies[*message].push_back(when.get<std::int64_t>());
        }
    }
    prune();
}

void DirectPolicy::save() const
{
    if (m_path.empty())
        return;
    Json replies = Json::object();
    for (const auto& [message, times] : m_replies)
        replies[std::to_string(message)] = times;
    writeFileAtomically(m_path, Json{{"last_interval", m_lastInterval}, {"replies", replies}}.dump() + "\n", "nocti");
}

} // namespace Nocti
