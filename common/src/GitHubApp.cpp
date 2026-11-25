// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GitHubApp.h"

#include "Log.h"

#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace Bots {

using Json = nlohmann::json;

namespace {

constexpr std::string_view Api = "https://api.github.com";
/// A JWT lives at most 10 minutes; its iat is set back a minute for clock drift.
constexpr std::int64_t JwtBefore = 60;
constexpr std::int64_t JwtAfter = 9 * 60;
/// An installation token is replaced this long before it ends.
constexpr std::int64_t Renew = 10 * 60;

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// "2026-11-18T21:00:00Z" as Unix seconds; 0 when it is not that form.
std::int64_t isoTime(const std::string& text)
{
    std::tm tm{};
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
                    &tm.tm_min, &tm.tm_sec)
        != 6)
        return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return std::int64_t(timegm(&tm));
}

} // namespace

std::string base64Url(std::string_view bytes)
{
    static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned v = (unsigned(static_cast<unsigned char>(bytes[i])) << 16)
            | (unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8)
            | unsigned(static_cast<unsigned char>(bytes[i + 2]));
        out += Alphabet[(v >> 18) & 63];
        out += Alphabet[(v >> 12) & 63];
        out += Alphabet[(v >> 6) & 63];
        out += Alphabet[v & 63];
    }
    if (const std::size_t rest = bytes.size() - i; rest) {
        unsigned v = unsigned(static_cast<unsigned char>(bytes[i])) << 16;
        if (rest == 2)
            v |= unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8;
        out += Alphabet[(v >> 18) & 63];
        out += Alphabet[(v >> 12) & 63];
        if (rest == 2)
            out += Alphabet[(v >> 6) & 63];
    }
    return out;
}

GitHubApp::GitHubApp(std::string appId, const std::string& privateKeyPem, std::string org, std::string userAgent,
                     HttpTransport transport, Now now)
    : m_appId(std::move(appId))
    , m_key(nullptr)
    , m_org(std::move(org))
    , m_userAgent(std::move(userAgent))
    , m_transport(std::move(transport))
    , m_now(now ? std::move(now) : Now(systemNow))
{
    BIO* bio = BIO_new_mem_buf(privateKeyPem.data(), int(privateKeyPem.size()));
    EVP_PKEY* key = bio ? PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(bio);
    if (!key || EVP_PKEY_get_base_id(key) != EVP_PKEY_RSA) {
        EVP_PKEY_free(key);
        throw std::runtime_error("the GitHub App's key is no RSA private key in PEM");
    }
    m_key = key;
}

GitHubApp::~GitHubApp()
{
    EVP_PKEY_free(static_cast<EVP_PKEY*>(m_key));
}

std::string GitHubApp::jwt(std::int64_t now) const
{
    const std::string header = base64Url(R"({"alg":"RS256","typ":"JWT"})");
    const std::string claims =
        base64Url(Json{{"iat", now - JwtBefore}, {"exp", now + JwtAfter}, {"iss", m_appId}}.dump());
    const std::string signedPart = header + "." + claims;

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    std::size_t length = 0;
    if (!context
        || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, static_cast<EVP_PKEY*>(m_key)) != 1
        || EVP_DigestSign(context.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(signedPart.data()),
                          signedPart.size())
            != 1)
        return {};
    std::string signature(length, '\0');
    if (EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &length,
                       reinterpret_cast<const unsigned char*>(signedPart.data()), signedPart.size())
        != 1)
        return {};
    signature.resize(length);
    return signedPart + "." + base64Url(signature);
}

std::string GitHubApp::token()
{
    const std::int64_t now = m_now();
    if (!m_token.empty() && now < m_tokenEnds - Renew)
        return m_token;

    const auto call = [&](const std::string& method, const std::string& path, long& status) -> Json {
        HttpRequest request;
        request.method = method;
        request.url = std::string(Api) + path;
        request.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
                           "User-Agent: " + m_userAgent, "Authorization: Bearer " + jwt(now)};
        const HttpResponse response = m_transport(request);
        status = response.status;
        const Json body = Json::parse(response.body, nullptr, false);
        if (status < 200 || status >= 300) {
            const std::string error = status ? std::format("{} {} {}", method, path, status) : response.error;
            if (error != m_lastError) {
                Log::info("github-app", "{}{}", error,
                          body.is_object() ? ": " + body.value("message", std::string()) : std::string());
                m_lastError = error;
            }
            return Json();
        }
        return body.is_discarded() ? Json() : body;
    };

    long status = 0;
    if (m_installation.empty()) {
        const Json installation = call("GET", std::format("/orgs/{}/installation", m_org), status);
        if (!installation.is_object() || !installation.contains("id"))
            return {};
        m_installation = installation["id"].dump();
    }
    const Json token = call("POST", std::format("/app/installations/{}/access_tokens", m_installation), status);
    if (status == 404)
        m_installation.clear(); // the app was installed again
    if (!token.is_object() || !token.contains("token") || !token["token"].is_string())
        return {};
    m_token = token["token"].get<std::string>();
    m_tokenEnds = isoTime(token.value("expires_at", std::string()));
    if (m_tokenEnds == 0)
        m_tokenEnds = now + 3600;
    m_lastError.clear();
    return m_token;
}

} // namespace Bots
