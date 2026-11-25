// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Away.h"

#include "Files.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <format>
#include <utility>

namespace Nocti {

using Json = nlohmann::json;

namespace {

constexpr std::array<std::string_view, 3> Names = {"home", "work", "asleep"};

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Json toJson(const Waiting& waiting)
{
    return {{"mark", waiting.mark},
            {"since", waiting.since},
            {"mode", modeName(waiting.mode)},
            {"channel", std::to_string(waiting.channel)},
            {"reply_to", waiting.replyTo ? Json(std::to_string(waiting.replyTo)) : Json()},
            {"text", waiting.text}};
}

std::optional<Waiting> fromJson(const Json& json)
{
    if (!json.is_object() || !json.contains("text") || !json["text"].is_string())
        return std::nullopt;
    Waiting waiting;
    waiting.mark = json.value("mark", "");
    waiting.since = json.value("since", std::int64_t(0));
    waiting.mode = parseMode(json.value("mode", "home")).value_or(Mode::Home);
    waiting.channel = parseSnowflake(json.value("channel", "")).value_or(0);
    if (json.contains("reply_to") && json["reply_to"].is_string())
        waiting.replyTo = parseSnowflake(json["reply_to"].get<std::string>()).value_or(0);
    waiting.text = json["text"].get<std::string>();
    if (waiting.mark.empty() || !waiting.channel)
        return std::nullopt;
    return waiting;
}

} // namespace

std::string modeName(Mode mode)
{
    return std::string(Names[std::size_t(mode)]);
}

std::optional<Mode> parseMode(std::string_view name)
{
    for (std::size_t i = 0; i < Names.size(); ++i)
        if (Names[i] == name)
            return Mode(i);
    return std::nullopt;
}

std::string modeMeans(Mode mode)
{
    switch (mode) {
    case Mode::Home:
        return "a post past the rules is a draft, which a check mark posts";
    case Mode::Work:
        return std::format(
            "no draft: the interval is {} min, a direct post past it waits its turn in the queue, and "
            "a post without direct waits for home (or is the approver's draft at once)",
            Away::AwayMinutes);
    case Mode::Asleep:
        return "as work, and what waits for home comes as silent drafts and one summary";
    }
    return {};
}

std::optional<std::int64_t> parseSpan(std::string_view text)
{
    if (text.size() < 2)
        return std::nullopt;
    const char unit = text.back();
    const std::int64_t scale = unit == 'm' ? 60 : unit == 'h' ? 3600 : unit == 'd' ? 86400 : 0;
    std::int64_t count = 0;
    const auto digits = text.substr(0, text.size() - 1);
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), count);
    if (!scale || ec != std::errc{} || end != digits.data() + digits.size() || count <= 0)
        return std::nullopt;
    const std::int64_t seconds = count * scale;
    if (seconds < 60 || seconds > Away::MostSpan)
        return std::nullopt;
    return seconds;
}

Away::Away(std::string path, Now now)
    : m_path(std::move(path))
    , m_now(now ? std::move(now) : Now(systemNow))
{
    load();
}

Mode Away::mode()
{
    if (m_mode != Mode::Home && m_until && m_now() >= m_until) {
        m_ranOut = m_mode;
        m_mode = Mode::Home;
        m_since = m_until;
        m_until = 0;
        save();
    }
    return m_mode;
}

std::int64_t Away::modeUntil()
{
    mode();
    return m_until;
}

Mode Away::setMode(Mode mode, std::int64_t seconds)
{
    const Mode before = this->mode();
    m_mode = mode;
    m_since = m_now();
    m_until = mode != Mode::Home && seconds ? m_since + seconds : 0;
    m_ranOut.reset();
    save();
    return before;
}

std::optional<Mode> Away::ranOut()
{
    mode();
    return std::exchange(m_ranOut, std::nullopt);
}

Snowflake Away::approver()
{
    if (m_approver && m_now() >= m_approverUntil) {
        m_approverRanOut = m_approver;
        m_approver = 0;
        m_approverUntil = 0;
        save();
    }
    return m_approver;
}

std::int64_t Away::approverUntil()
{
    approver();
    return m_approverUntil;
}

void Away::setApprover(Snowflake user, std::int64_t seconds)
{
    m_approver = user;
    m_approverUntil = m_now() + seconds;
    m_approverRanOut.reset();
    save();
}

bool Away::clearApprover()
{
    if (!approver())
        return false;
    m_approver = 0;
    m_approverUntil = 0;
    save();
    return true;
}

std::optional<Snowflake> Away::approverRanOut()
{
    approver();
    return std::exchange(m_approverRanOut, std::nullopt);
}

std::string Away::nextMark(char kind)
{
    return std::format("{}{}", kind, m_nextMark++);
}

std::string Away::hold(Snowflake channel, Snowflake replyTo, const std::string& text)
{
    Waiting waiting{nextMark('h'), m_now(), mode(), channel, replyTo, text};
    m_held.push_back(waiting);
    save();
    return waiting.mark;
}

std::string Away::queue(Snowflake channel, Snowflake replyTo, const std::string& text)
{
    Waiting waiting{nextMark('q'), m_now(), mode(), channel, replyTo, text};
    m_queue.push_back(waiting);
    save();
    return waiting.mark;
}

std::optional<Waiting> Away::takeQueued(const std::string& how)
{
    if (m_queue.empty())
        return std::nullopt;
    Waiting front = m_queue.front();
    m_queue.pop_front();
    m_gone[front.mark] = {how, m_now()};
    save();
    return front;
}

std::vector<Waiting> Away::takeHeld(const std::string& how)
{
    std::vector<Waiting> out;
    out.swap(m_held);
    for (const auto& waiting : out)
        m_gone[waiting.mark] = {how, m_now()};
    if (!out.empty())
        save();
    return out;
}

void Away::noteGone(const std::string& mark, const std::string& how)
{
    m_gone[mark] = {how, m_now()};
    save();
}

std::string Away::cancel(const std::string& mark)
{
    prune();
    const auto matches = [&](const Waiting& waiting) {
        return waiting.mark == mark;
    };
    if (const auto it = std::find_if(m_queue.begin(), m_queue.end(), matches); it != m_queue.end()) {
        m_queue.erase(it);
    } else if (const auto held = std::find_if(m_held.begin(), m_held.end(), matches); held != m_held.end()) {
        m_held.erase(held);
    } else if (const auto gone = m_gone.find(mark); gone != m_gone.end()) {
        return gone->second.first;
    } else {
        return "no such post";
    }
    m_gone[mark] = {"taken back", m_now()};
    save();
    return {};
}

void Away::prune()
{
    const std::int64_t cutoff = m_now() - 86400;
    std::erase_if(m_gone, [&](const auto& entry) {
        return entry.second.second <= cutoff;
    });
}

void Away::load()
{
    if (m_path.empty())
        return;
    const auto text = readFile(m_path);
    if (!text)
        return;
    const Json json = Json::parse(*text, nullptr, false);
    if (!json.is_object())
        return;
    m_mode = parseMode(json.value("mode", "home")).value_or(Mode::Home);
    m_since = json.value("since", std::int64_t(0));
    m_until = json.value("until", std::int64_t(0));
    m_approver = parseSnowflake(json.value("approver", "")).value_or(0);
    m_approverUntil = json.value("approver_until", std::int64_t(0));
    m_nextMark = std::max<std::int64_t>(1, json.value("next_mark", std::int64_t(1)));
    for (const auto& item : json.value("queue", Json::array()))
        if (auto waiting = fromJson(item))
            m_queue.push_back(std::move(*waiting));
    for (const auto& item : json.value("held", Json::array()))
        if (auto waiting = fromJson(item))
            m_held.push_back(std::move(*waiting));
    if (json.contains("gone") && json["gone"].is_object())
        for (const auto& [mark, entry] : json["gone"].items())
            if (entry.is_object())
                m_gone[mark] = {entry.value("how", ""), entry.value("when", std::int64_t(0))};
    prune();
}

void Away::save()
{
    if (m_path.empty())
        return;
    prune();
    Json queue = Json::array();
    for (const auto& waiting : m_queue)
        queue.push_back(toJson(waiting));
    Json held = Json::array();
    for (const auto& waiting : m_held)
        held.push_back(toJson(waiting));
    Json gone = Json::object();
    for (const auto& [mark, entry] : m_gone)
        gone[mark] = {{"how", entry.first}, {"when", entry.second}};
    const Json state = {{"mode", modeName(m_mode)},
                        {"since", m_since},
                        {"until", m_until},
                        {"approver", m_approver ? std::to_string(m_approver) : std::string()},
                        {"approver_until", m_approverUntil},
                        {"next_mark", m_nextMark},
                        {"queue", queue},
                        {"held", held},
                        {"gone", gone}};
    writeFileAtomically(m_path, state.dump(2) + "\n", "nocti");
}

} // namespace Nocti
