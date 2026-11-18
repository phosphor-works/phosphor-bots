// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HttpServer.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Bots {

namespace {

constexpr std::size_t MaxHead = 16 * 1024;
constexpr int HeadTimeoutMs = 10'000;

const char* reason(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "Status";
    }
}

void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        sent += std::size_t(n);
    }
}

} // namespace

HttpServer::HttpServer(Handler handler)
    : m_handler(std::move(handler))
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::parseHead(const std::string& head, ServerRequest& request)
{
    const auto lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos)
        return false;
    const std::string line = head.substr(0, lineEnd);
    const auto space1 = line.find(' ');
    const auto space2 = space1 == std::string::npos ? std::string::npos : line.find(' ', space1 + 1);
    if (space2 == std::string::npos || !line.substr(space2 + 1).starts_with("HTTP/1."))
        return false;
    request.method = line.substr(0, space1);
    const std::string target = line.substr(space1 + 1, space2 - space1 - 1);
    if (!target.starts_with('/'))
        return false;
    const auto question = target.find('?');
    request.path = target.substr(0, question);
    request.query = question == std::string::npos ? std::string() : target.substr(question + 1);
    std::size_t at = lineEnd + 2;
    while (at < head.size()) {
        const auto end = head.find("\r\n", at);
        if (end == std::string::npos || end == at)
            break;
        const std::string header = head.substr(at, end - at);
        const auto colon = header.find(':');
        if (colon == std::string::npos || colon == 0)
            return false;
        std::string name = header.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        std::string value = header.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        request.headers[name] = value;
        at = end + 2;
    }
    return true;
}

bool HttpServer::start(const std::string& address)
{
    const auto colon = address.rfind(':');
    if (colon == std::string::npos) {
        Log::info("http", "{}: not host:port", address);
        return false;
    }
    sockaddr_in where{};
    where.sin_family = AF_INET;
    where.sin_port = htons(std::uint16_t(std::stoi(address.substr(colon + 1))));
    if (::inet_pton(AF_INET, address.substr(0, colon).c_str(), &where.sin_addr) != 1) {
        Log::info("http", "{}: not an IPv4 address", address);
        return false;
    }
    m_socket = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    const int yes = 1;
    ::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    if (m_socket < 0 || ::bind(m_socket, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0
        || ::listen(m_socket, 16) != 0) {
        Log::info("http", "cannot listen on {}: {}", address, std::strerror(errno));
        if (m_socket >= 0)
            ::close(m_socket);
        m_socket = -1;
        return false;
    }
    socklen_t length = sizeof where;
    ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&where), &length);
    m_port = ntohs(where.sin_port);
    m_thread = std::thread([this] {
        run();
    });
    return true;
}

void HttpServer::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
    if (m_socket >= 0)
        ::close(m_socket);
    m_socket = -1;
}

void HttpServer::run()
{
    while (!m_stop) {
        pollfd listening{m_socket, POLLIN, 0};
        if (::poll(&listening, 1, 500) <= 0)
            continue;
        const int client = ::accept4(m_socket, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        serve(client);
        ::close(client);
    }
}

void HttpServer::serve(int client)
{
    std::string head;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(HeadTimeoutMs);
    while (head.find("\r\n\r\n") == std::string::npos) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        pollfd fd{client, POLLIN, 0};
        if (left <= 0 || ::poll(&fd, 1, int(left)) <= 0)
            return;
        char buffer[4096];
        const ssize_t n = ::recv(client, buffer, sizeof buffer, 0);
        if (n <= 0)
            return;
        head.append(buffer, std::size_t(n));
        if (head.size() > MaxHead)
            return;
    }
    ServerRequest request;
    ServerResponse response;
    if (!parseHead(head.substr(0, head.find("\r\n\r\n") + 2), request))
        response = {400, "application/json", R"({"error":"a bad request"})"};
    else
        response = m_handler(request);
    sendAll(client,
            std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\n"
                        "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                        response.status, reason(response.status), response.contentType, response.body.size())
                + response.body);
}

} // namespace Bots
