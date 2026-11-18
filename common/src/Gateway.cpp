// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Gateway.h"

#include "Discord.h"
#include "Log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>

#include <curl/curl.h>
#include <poll.h>

namespace Bots {

namespace {

using std::chrono::milliseconds;
using SteadyClock = std::chrono::steady_clock;

/// The longest a poll() waits, so that stop() is seen soon.
constexpr milliseconds PollSlice{1000};
/// A message of the gateway past this is no message of Discord's.
constexpr std::size_t MaxMessage = std::size_t(64) << 20;

struct CurlHandle
{
    CURL* curl = curl_easy_init();
    ~CurlHandle()
    {
        curl_easy_cleanup(curl);
    }
};

bool sendText(CURL* curl, const std::string& text, curl_socket_t socket)
{
    std::size_t offset = 0;
    for (int tries = 0; offset < text.size() && tries < 50; ++tries) {
        std::size_t sent = 0;
        const CURLcode code = curl_ws_send(curl, text.data() + offset, text.size() - offset, &sent, 0, CURLWS_TEXT);
        offset += sent;
        if (code == CURLE_OK && offset >= text.size())
            return true;
        if (code != CURLE_OK && code != CURLE_AGAIN)
            return false;
        pollfd fd{socket, POLLOUT, 0};
        ::poll(&fd, 1, 200);
    }
    return offset >= text.size();
}

/// The close code of a CLOSE frame's payload, 0 when it has none.
int closeCode(const std::string& payload)
{
    if (payload.size() < 2)
        return 0;
    return (int(static_cast<unsigned char>(payload[0])) << 8) | int(static_cast<unsigned char>(payload[1]));
}

} // namespace

Gateway::Gateway(std::string token, std::string name, std::string userAgent, EventHandler onEvent, FatalHandler onFatal)
    : m_token(std::move(token))
    , m_userAgent(std::move(userAgent))
    , m_onEvent(std::move(onEvent))
    , m_onFatal(std::move(onFatal))
    , m_session(m_token, std::move(name))
{
}

Gateway::~Gateway()
{
    stop();
}

void Gateway::start()
{
    m_thread = std::thread([this] {
        run();
    });
}

void Gateway::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

std::string Gateway::gatewayUrl(Discord& rest)
{
    const DiscordResult result = rest.gatewayBot();
    if (result.ok() && result.body.is_object() && result.body.contains("url") && result.body["url"].is_string())
        return result.body["url"].get<std::string>();
    if (result.status == 401) {
        m_onFatal("Discord refused the token (401 from GET /gateway/bot)");
        m_stop = true;
    } else {
        Log::info("gateway", "GET /gateway/bot: {} {}", result.status,
                  result.status == 0 ? result.error : result.body.dump());
    }
    return {};
}

void Gateway::run()
{
    Discord rest(m_token, m_userAgent, makeCurlTransport(std::chrono::seconds(15)));
    std::string url;
    milliseconds backoff{1000};
    while (!m_stop) {
        if (url.empty())
            url = gatewayUrl(rest);
        if (m_stop)
            break;
        bool opened = false;
        if (!url.empty())
            opened = connection(m_session.connectUrl(url));
        if (m_session.fatal()) {
            m_onFatal(m_session.fatalReason());
            break;
        }
        milliseconds wait = m_session.reconnectDelay();
        if (!opened || !m_session.ready()) {
            // The network or Discord is out of reach, or the connection never
            // became ready: wait longer each time, and ask for the gateway's
            // address again when none opened.
            wait = std::max(wait, backoff);
            backoff = std::min(backoff * 2, milliseconds(60000));
            if (!opened)
                url.clear();
        } else {
            backoff = milliseconds(1000);
        }
        for (auto waited = milliseconds(0); waited < wait && !m_stop; waited += milliseconds(100))
            std::this_thread::sleep_for(milliseconds(100));
    }
}

bool Gateway::connection(const std::string& url)
{
    CurlHandle handle;
    CURL* curl = handle.curl;
    if (!curl)
        return false;
    char error[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "wss");
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    if (const CURLcode code = curl_easy_perform(curl); code != CURLE_OK) {
        Log::info("gateway", "could not connect to {}: {}", url, error[0] ? error : curl_easy_strerror(code));
        return false;
    }
    curl_socket_t socket = CURL_SOCKET_BAD;
    curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
    if (socket == CURL_SOCKET_BAD)
        return false;

    m_session.connected(SteadyClock::now());
    std::string message;
    std::vector<char> buffer(64 * 1024);
    bool closedByDiscord = false;

    const auto deliver = [&](const std::vector<std::string>& payloads) {
        for (const auto& payload : payloads) {
            if (!sendText(curl, payload, socket))
                return false;
        }
        for (auto& event : m_session.takeEvents())
            m_onEvent(std::move(event));
        return true;
    };

    while (!m_stop && !m_session.mustReconnect()) {
        const auto now = SteadyClock::now();
        const auto untilDeadline = std::chrono::duration_cast<milliseconds>(m_session.nextDeadline() - now);
        const int timeout = int(std::clamp(untilDeadline, milliseconds(0), PollSlice).count());
        pollfd fd{socket, POLLIN, 0};
        const int polled = ::poll(&fd, 1, timeout);
        if (polled < 0 && errno != EINTR)
            break;

        // Read every frame there is.
        bool failed = false;
        for (;;) {
            std::size_t received = 0;
            const curl_ws_frame* meta = nullptr;
            const CURLcode code = curl_ws_recv(curl, buffer.data(), buffer.size(), &received, &meta);
            if (code == CURLE_AGAIN)
                break;
            if (code != CURLE_OK) {
                m_session.closed(0);
                Log::info("gateway", "the connection ended: {}", curl_easy_strerror(code));
                failed = true;
                break;
            }
            if (!meta)
                continue;
            if (meta->flags & CURLWS_CLOSE) {
                message.append(buffer.data(), received);
                if (meta->bytesleft > 0)
                    continue;
                m_session.closed(closeCode(message));
                closedByDiscord = true;
                failed = true;
                break;
            }
            if (!(meta->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)))
                continue; // a PING or PONG, which libcurl answers
            message.append(buffer.data(), received);
            if (message.size() > MaxMessage) {
                m_session.closed(0);
                failed = true;
                break;
            }
            if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
                const auto payloads = m_session.receive(message, SteadyClock::now());
                message.clear();
                if (!deliver(payloads)) {
                    m_session.closed(0);
                    failed = true;
                    break;
                }
            }
        }
        if (failed)
            break;
        if (!deliver(m_session.tick(SteadyClock::now()))) {
            m_session.closed(0);
            break;
        }
    }

    if (!m_session.reconnectReason().empty())
        Log::info("gateway", "reconnecting: {}", m_session.reconnectReason());
    if (!closedByDiscord) {
        // 4000, not 1000: a normal close would end the session, and the next
        // connection resumes it.
        const unsigned char code[2] = {0x0f, 0xa0};
        std::size_t sent = 0;
        curl_ws_send(curl, code, sizeof code, &sent, 0, CURLWS_CLOSE);
    }
    return true;
}

} // namespace Bots
