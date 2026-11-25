// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Bots {

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers; ///< "Name: value"
    std::string body;
};

struct HttpResponse
{
    long status = 0; ///< 0: no answer (see error)
    std::map<std::string, std::string> headers; ///< names in lowercase
    std::string body;
    std::string error; ///< why no answer came

    std::string header(const std::string& lowercaseName) const
    {
        const auto it = headers.find(lowercaseName);
        return it == headers.end() ? std::string() : it->second;
    }
};

/// Sends one request and waits for its answer. The tests give a fake.
using HttpTransport = std::function<HttpResponse(const HttpRequest&)>;

/// A transport through libcurl: one handle, reused for its connections, for
/// one thread. HTTPS only to the hosts the caller names; no redirects.
class CurlTransport
{
public:
    explicit CurlTransport(std::chrono::seconds timeout = std::chrono::seconds(20));
    ~CurlTransport();
    CurlTransport(const CurlTransport&) = delete;
    CurlTransport& operator=(const CurlTransport&) = delete;

    HttpResponse operator()(const HttpRequest& request);

private:
    void* m_curl;
    std::chrono::seconds m_timeout;
};

/// A transport as an HttpTransport, owning its CurlTransport.
HttpTransport makeCurlTransport(std::chrono::seconds timeout = std::chrono::seconds(20));

/// curl_global_init, once, before any thread starts.
void initHttp();

} // namespace Bots
