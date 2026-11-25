// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "People.h"

#include "Files.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>

namespace Nocti {

using Json = nlohmann::json;

namespace {

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Json ids(const std::set<Snowflake>& set)
{
    Json out = Json::array();
    for (const auto id : set)
        out.push_back(std::to_string(id));
    return out;
}

std::set<Snowflake> idsOf(const Json& json)
{
    std::set<Snowflake> out;
    if (json.is_array()) {
        for (const auto& id : json) {
            if (id.is_string())
                if (const auto parsed = parseSnowflake(id.get<std::string>()))
                    out.insert(*parsed);
        }
    }
    return out;
}

} // namespace

std::string utcTime(std::int64_t seconds)
{
    const std::time_t time = std::time_t(seconds);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M UTC", &tm);
    return text;
}

People::People(Snowflake owner, std::set<Snowflake> trustedAtStart, std::string path, Now now)
    : m_owner(owner)
    , m_atStart(std::move(trustedAtStart))
    , m_path(std::move(path))
    , m_now(now ? std::move(now) : Now(systemNow))
{
    load();
}

int People::hourlyWeight(Level level)
{
    switch (level) {
    case Level::Owner:
        return -1; // no limit
    case Level::Trusted:
        return 30;
    case Level::Allowed:
        return 10;
    case Level::Stranger:
        return 0;
    }
    return 0;
}

void People::expire()
{
    const std::int64_t now = m_now();
    bool changed = false;
    for (auto it = m_allowed.begin(); it != m_allowed.end();) {
        if (it->second.until && it->second.until <= now) {
            it = m_allowed.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        save();
}

Level People::level(Snowflake user)
{
    if (user == m_owner)
        return Level::Owner;
    if (trusted().contains(user))
        return Level::Trusted;
    expire();
    if (m_allowed.contains(user))
        return Level::Allowed;
    return Level::Stranger;
}

std::set<Snowflake> People::trusted() const
{
    std::set<Snowflake> out = m_trusted;
    for (const auto id : m_atStart) {
        if (!m_untrusted.contains(id))
            out.insert(id);
    }
    out.erase(m_owner);
    return out;
}

bool People::take(Snowflake user)
{
    const int weight = hourlyWeight(level(user));
    if (weight < 0)
        return true;
    auto& times = m_heard[user];
    const std::int64_t now = m_now();
    while (!times.empty() && now - times.front() >= 3600)
        times.pop_front();
    if (int(times.size()) >= weight)
        return false;
    times.push_back(now);
    return true;
}

std::string People::heard(Snowflake user)
{
    const int weight = hourlyWeight(level(user));
    if (weight <= 0)
        return {};
    auto& times = m_heard[user];
    const std::int64_t now = m_now();
    while (!times.empty() && now - times.front() >= 3600)
        times.pop_front();
    return std::format("{} of {} this hour", times.size(), weight);
}

std::string People::describe(Snowflake user)
{
    switch (level(user)) {
    case Level::Owner:
        return "owner";
    case Level::Trusted:
        return "trusted";
    case Level::Allowed: {
        const Allow& allow = m_allowed.at(user);
        return allow.until ? "allowed until " + utcTime(allow.until) : "allowed for good";
    }
    case Level::Stranger:
        return "stranger";
    }
    return "stranger";
}

void People::trust(Snowflake user)
{
    m_untrusted.erase(user);
    if (!m_atStart.contains(user))
        m_trusted.insert(user);
    save();
}

void People::untrust(Snowflake user)
{
    m_trusted.erase(user);
    if (m_atStart.contains(user))
        m_untrusted.insert(user);
    save();
}

std::optional<Allow> People::allow(Snowflake user, const std::string& name, std::int64_t seconds)
{
    expire();
    std::optional<Allow> before;
    if (const auto it = m_allowed.find(user); it != m_allowed.end())
        before = it->second;
    const std::int64_t now = m_now();
    m_allowed[user] = {name, now, seconds ? now + seconds : 0};
    save();
    return before;
}

bool People::deny(Snowflake user)
{
    const bool had = m_allowed.erase(user) > 0;
    if (had)
        save();
    return had;
}

std::map<Snowflake, Allow> People::allowed()
{
    expire();
    return m_allowed;
}

std::optional<std::int64_t> People::freeze(Snowflake user)
{
    const Level standing = level(user);
    if (standing == Level::Owner || standing == Level::Trusted)
        return std::nullopt;
    const std::int64_t now = m_now();
    Ice& ice = m_ice[user];
    std::erase_if(ice.strikes, [&](std::int64_t when) {
        return now - when >= 7 * 86400;
    });
    const bool again = !ice.strikes.empty();
    ice.strikes.push_back(now);
    ice.until = now + (again ? 86400 : 7200);
    save();
    return ice.until;
}

bool People::unfreeze(Snowflake user)
{
    const auto it = m_ice.find(user);
    if (it == m_ice.end() || it->second.until <= m_now())
        return false;
    it->second.until = 0; // the strikes stay
    save();
    return true;
}

std::optional<std::int64_t> People::iced(Snowflake user)
{
    const auto it = m_ice.find(user);
    if (it == m_ice.end() || it->second.until <= m_now())
        return std::nullopt;
    return it->second.until;
}

int People::strikes(Snowflake user)
{
    const auto it = m_ice.find(user);
    if (it == m_ice.end())
        return 0;
    const std::int64_t now = m_now();
    return int(std::count_if(it->second.strikes.begin(), it->second.strikes.end(), [&](std::int64_t when) {
        return now - when < 7 * 86400;
    }));
}

void People::load()
{
    if (m_path.empty())
        return;
    std::string error;
    const auto text = readFile(m_path, &error);
    if (!text) {
        if (!error.empty())
            Log::info("nocti", "{}: {}; starting with no one trusted or allowed beyond the env file", m_path, error);
        return;
    }
    const Json json = Json::parse(*text, nullptr, false);
    if (!json.is_object()) {
        Log::info("nocti", "{}: not JSON; starting with no one trusted or allowed beyond the env file", m_path);
        return;
    }
    m_trusted = idsOf(json.value("trusted", Json::array()));
    m_untrusted = idsOf(json.value("untrusted", Json::array()));
    if (json.contains("ice") && json["ice"].is_object()) {
        for (const auto& [id, ice] : json["ice"].items()) {
            if (const auto user = parseSnowflake(id)) {
                Ice parsed{ice.value("until", std::int64_t(0)), {}};
                for (const auto& when : ice.value("strikes", Json::array()))
                    if (when.is_number_integer())
                        parsed.strikes.push_back(when.get<std::int64_t>());
                m_ice[*user] = std::move(parsed);
            }
        }
    }
    if (json.contains("allowed") && json["allowed"].is_object()) {
        for (const auto& [id, allow] : json["allowed"].items()) {
            if (const auto user = parseSnowflake(id))
                m_allowed[*user] = {allow.value("name", ""), allow.value("since", std::int64_t(0)),
                                    allow.value("until", std::int64_t(0))};
        }
    }
}

void People::save() const
{
    if (m_path.empty())
        return;
    Json allowed = Json::object();
    for (const auto& [id, allow] : m_allowed)
        allowed[std::to_string(id)] = {{"name", allow.name}, {"since", allow.since}, {"until", allow.until}};
    Json ice = Json::object();
    for (const auto& [id, entry] : m_ice)
        ice[std::to_string(id)] = {{"until", entry.until}, {"strikes", entry.strikes}};
    const Json state = {{"trusted", ids(m_trusted)},
                        {"untrusted", ids(m_untrusted)},
                        {"allowed", allowed},
                        {"ice", ice}};
    writeFileAtomically(m_path, state.dump(2) + "\n", "nocti");
}

} // namespace Nocti
