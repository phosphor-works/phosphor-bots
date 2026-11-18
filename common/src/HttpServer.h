// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace Bots {

struct ServerRequest
{
    std::string method;
    std::string path; ///< without the query
    std::string query;
    std::map<std::string, std::string> headers; ///< names in lowercase
};

struct ServerResponse
{
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
};

/// A small HTTP/1.1 server for one reverse proxy in front of it: one request
/// a connection, one connection at a time, no request body, headers of 16 KiB
/// at most, 10 s to send them. It listens on one address (a jail's), never
/// the internet's.
class HttpServer
{
public:
    using Handler = std::function<ServerResponse(const ServerRequest&)>;

    explicit HttpServer(Handler handler);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Listens on @p address ("10.17.0.7:8090", "127.0.0.1:0") and serves on a
    /// thread of its own. False (said in the log) when it cannot.
    bool start(const std::string& address);
    void stop();
    /// The port it listens on (the one picked for port 0).
    int port() const
    {
        return m_port;
    }

    /// Parses a request's head (the request line and the headers), for the
    /// tests. False when it is not one.
    static bool parseHead(const std::string& head, ServerRequest& request);

private:
    void run();
    void serve(int client);

    Handler m_handler;
    int m_socket = -1;
    int m_port = 0;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

} // namespace Bots
