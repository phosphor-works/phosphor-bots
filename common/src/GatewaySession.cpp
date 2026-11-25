// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GatewaySession.h"

#include <algorithm>
#include <format>
#include <memory>
#include <random>

namespace Bots {

using Json = nlohmann::json;

namespace {

enum Op {
    Dispatch = 0,
    Heartbeat = 1,
    Identify = 2,
    Resume = 6,
    Reconnect = 7,
    InvalidSession = 9,
    Hello = 10,
    HeartbeatAck = 11,
};

/// Close codes after which Discord takes the bot no more: reconnecting
/// changes nothing until a person does.
std::string fatalClose(int code)
{
    switch (code) {
    case 4004:
        return "Discord refused the token (4004: authentication failed)";
    case 4010:
        return "Discord refused the shard (4010)";
    case 4011:
        return "Discord asks for sharding (4011)";
    case 4012:
        return "Discord refused the gateway version (4012)";
    case 4013:
        return "Discord refused the intents (4013: invalid intents)";
    case 4014:
        return "Discord refused the intents (4014: turn on Message Content Intent in the Developer Portal)";
    default:
        return {};
    }
}

} // namespace

GatewaySession::GatewaySession(std::string token, std::string name, Random random)
    : m_token(std::move(token))
    , m_name(std::move(name))
    , m_random(std::move(random))
{
    if (!m_random) {
        m_random = [engine = std::make_shared<std::mt19937_64>(std::random_device{}())]() {
            return std::uniform_real_distribution<double>(0.0, 1.0)(*engine);
        };
    }
}

void GatewaySession::connected(TimePoint now)
{
    m_connectedAt = now;
    m_nextHeartbeat.reset();
    m_acknowledged = true;
    m_ready = false;
    m_reconnect = false;
    m_delay = std::chrono::milliseconds(0);
    m_reason.clear();
}

std::string GatewaySession::connectUrl(const std::string& gatewayUrl) const
{
    const std::string& base = (!m_sessionId.empty() && !m_resumeUrl.empty()) ? m_resumeUrl : gatewayUrl;
    return base + (base.ends_with('/') ? "" : "/") + "?v=10&encoding=json";
}

std::string GatewaySession::heartbeat() const
{
    return Json{{"op", Heartbeat}, {"d", m_sequence ? Json(*m_sequence) : Json()}}.dump();
}

void GatewaySession::reconnect(std::string reason, bool keepSession)
{
    m_reconnect = true;
    m_reason = std::move(reason);
    if (!keepSession) {
        m_sessionId.clear();
        m_resumeUrl.clear();
        m_sequence.reset();
    }
}

std::vector<std::string> GatewaySession::receive(const std::string& text, TimePoint now)
{
    std::vector<std::string> out;
    const Json payload = Json::parse(text, nullptr, false);
    if (!payload.is_object() || !payload.contains("op") || !payload["op"].is_number_integer())
        return out;
    const int op = payload["op"].get<int>();
    const Json& data = payload.contains("d") ? payload["d"] : Json();

    switch (op) {
    case Hello: {
        const long long interval = data.is_object() ? data.value("heartbeat_interval", 41250LL) : 41250LL;
        m_interval = std::chrono::milliseconds(std::max(1000LL, interval));
        // The first heartbeat comes after a random part of the interval, as
        // Discord asks, so that many clients do not beat together.
        m_nextHeartbeat = now + std::chrono::milliseconds(long(double(m_interval.count()) * m_random()));
        m_acknowledged = true;
        if (!m_sessionId.empty() && m_sequence) {
            out.push_back(
                Json{{"op", Resume}, {"d", {{"token", m_token}, {"session_id", m_sessionId}, {"seq", *m_sequence}}}}
                    .dump());
        } else {
            out.push_back(Json{{"op", Identify},
                               {"d",
                                {{"token", m_token},
                                 {"intents", Intents},
                                 {"properties", {{"os", "freebsd"}, {"browser", m_name}, {"device", m_name}}}}}}
                              .dump());
        }
        break;
    }
    case Heartbeat:
        out.push_back(heartbeat());
        break;
    case HeartbeatAck:
        m_acknowledged = true;
        break;
    case Reconnect:
        reconnect("Discord asked for a new connection (RECONNECT)", true);
        break;
    case InvalidSession: {
        const bool resumable = data.is_boolean() && data.get<bool>();
        reconnect(resumable ? "the session is invalid, but resumable" : "the session is invalid", resumable);
        // Discord asks for a wait of 1 to 5 s before the next IDENTIFY.
        m_delay = std::chrono::milliseconds(1000 + long(4000.0 * m_random()));
        break;
    }
    case Dispatch: {
        if (payload.contains("s") && payload["s"].is_number_integer())
            m_sequence = payload["s"].get<long long>();
        const std::string type = payload.value("t", "");
        if (type == "READY" && data.is_object()) {
            m_sessionId = data.value("session_id", "");
            m_resumeUrl = data.value("resume_gateway_url", "");
            if (data.contains("user") && data["user"].is_object()) {
                m_userName = data["user"].value("username", "");
                m_userId = data["user"].value("id", "");
            }
            m_ready = true;
        } else if (type == "RESUMED") {
            m_ready = true;
        }
        m_events.push_back({type, data});
        break;
    }
    default:
        break;
    }
    return out;
}

std::vector<std::string> GatewaySession::tick(TimePoint now)
{
    std::vector<std::string> out;
    if (m_reconnect)
        return out;
    if (!m_ready && now - m_connectedAt >= 3 * m_interval) {
        // A session that never became ready cannot be trusted to resume.
        reconnect(std::format("the connection brought no READY or RESUMED in {} s (3 heartbeat intervals)",
                              double((3 * m_interval).count()) / 1000.0),
                  false);
        return out;
    }
    if (m_nextHeartbeat && now >= *m_nextHeartbeat) {
        if (!m_acknowledged) {
            reconnect("Discord acknowledged no heartbeat in an interval (a zombie connection)", true);
            return out;
        }
        out.push_back(heartbeat());
        m_acknowledged = false;
        m_nextHeartbeat = now + m_interval;
    }
    return out;
}

void GatewaySession::closed(int code)
{
    if (auto reason = fatalClose(code); !reason.empty()) {
        m_fatal = std::move(reason);
        reconnect(m_fatal, false);
        return;
    }
    // 4007: a sequence it does not know; 4009: the session timed out. Both
    // need a new session; any other close may resume.
    const bool keep = code != 4007 && code != 4009;
    reconnect(code ? std::format("Discord closed the connection ({})", code) : "the connection ended", keep);
}

GatewaySession::TimePoint GatewaySession::nextDeadline() const
{
    TimePoint deadline = m_connectedAt + 3 * m_interval;
    if (m_ready)
        deadline = TimePoint::max();
    if (m_nextHeartbeat && *m_nextHeartbeat < deadline)
        deadline = *m_nextHeartbeat;
    return deadline;
}

std::deque<GatewayEvent> GatewaySession::takeEvents()
{
    return std::exchange(m_events, {});
}

} // namespace Bots
