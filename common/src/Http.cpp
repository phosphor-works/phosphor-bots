// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Http.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <curl/curl.h>

namespace Bots {

namespace {

size_t writeBody(char* data, size_t size, size_t count, void* user)
{
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t writeHeader(char* data, size_t size, size_t count, void* user)
{
    auto* headers = static_cast<std::map<std::string, std::string>*>(user);
    std::string_view line(data, size * count);
    // A new response (after a 100 Continue, say) starts its headers again.
    if (line.starts_with("HTTP/"))
        headers->clear();
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
        std::string name(line.substr(0, colon));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
            value.remove_suffix(1);
        (*headers)[name] = std::string(value);
    }
    return size * count;
}

} // namespace

void initHttp()
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("curl_global_init failed");
}

CurlTransport::CurlTransport(std::chrono::seconds timeout)
    : m_curl(curl_easy_init())
    , m_timeout(timeout)
{
    if (!m_curl)
        throw std::runtime_error("curl_easy_init failed");
}

CurlTransport::~CurlTransport()
{
    curl_easy_cleanup(static_cast<CURL*>(m_curl));
}

HttpResponse CurlTransport::operator()(const HttpRequest& request)
{
    CURL* curl = static_cast<CURL*>(m_curl);
    curl_easy_reset(curl);
    HttpResponse response;
    curl_slist* headers = nullptr;
    for (const auto& header : request.headers)
        headers = curl_slist_append(headers, header.c_str());
    char error[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (request.method == "HEAD")
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (!request.body.empty() || request.method == "POST" || request.method == "PATCH" || request.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(request.body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, long(m_timeout.count()));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    // An answer past 32 MiB is no answer of these APIs.
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, curl_off_t(32) << 20);

    const CURLcode code = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        response.status = 0;
        response.error = error[0] ? error : curl_easy_strerror(code);
        return response;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

HttpTransport makeCurlTransport(std::chrono::seconds timeout)
{
    auto transport = std::make_shared<CurlTransport>(timeout);
    return [transport](const HttpRequest& request) {
        return (*transport)(request);
    };
}

} // namespace Bots
