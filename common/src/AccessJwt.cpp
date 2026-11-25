// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AccessJwt.h"

#include "Log.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>

#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

namespace Bots {

using Json = nlohmann::json;

namespace {

/// Keys are fetched again at most this often, also for an unknown key id.
constexpr std::int64_t KeysRefresh = 300;
/// The clock skew a token's times are allowed.
constexpr std::int64_t Leeway = 60;

std::int64_t systemNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

EVP_PKEY* rsaKey(const std::string& modulus, const std::string& exponent)
{
    std::unique_ptr<BIGNUM, decltype(&BN_free)> n(
        BN_bin2bn(reinterpret_cast<const unsigned char*>(modulus.data()), int(modulus.size()), nullptr), BN_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> e(
        BN_bin2bn(reinterpret_cast<const unsigned char*>(exponent.data()), int(exponent.size()), nullptr), BN_free);
    std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!n || !e || !builder || !OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N, n.get())
        || !OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E, e.get()))
        return nullptr;
    std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(builder.get()),
                                                                   OSSL_PARAM_free);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* key = nullptr;
    if (!params || !context || EVP_PKEY_fromdata_init(context.get()) != 1
        || EVP_PKEY_fromdata(context.get(), &key, EVP_PKEY_PUBLIC_KEY, params.get()) != 1)
        return nullptr;
    return key;
}

} // namespace

std::optional<std::string> base64UrlDecode(std::string_view text)
{
    static const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '-' || c == '+')
            return 62;
        if (c == '_' || c == '/')
            return 63;
        return -1;
    };
    while (!text.empty() && text.back() == '=')
        text.remove_suffix(1);
    if (text.size() % 4 == 1)
        return std::nullopt;
    std::string out;
    out.reserve(text.size() * 3 / 4);
    unsigned buffer = 0;
    int bits = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0)
            return std::nullopt;
        buffer = (buffer << 6) | unsigned(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

AccessJwt::AccessJwt(std::string team, std::string audience, HttpTransport transport, Now now)
    : m_team(std::move(team))
    , m_audience(std::move(audience))
    , m_transport(std::move(transport))
    , m_now(now ? std::move(now) : Now(systemNow))
{
}

AccessJwt::~AccessJwt()
{
    for (auto& [kid, key] : m_keys)
        EVP_PKEY_free(static_cast<EVP_PKEY*>(key));
}

std::size_t AccessJwt::loadKeys(const std::string& jwks)
{
    const Json document = Json::parse(jwks, nullptr, false);
    if (!document.is_object() || !document.contains("keys") || !document["keys"].is_array())
        return 0;
    std::map<std::string, void*> keys;
    for (const auto& key : document["keys"]) {
        if (key.value("kty", "") != "RSA" || !key.contains("n") || !key.contains("e") || !key.contains("kid"))
            continue;
        const auto n = base64UrlDecode(key.value("n", ""));
        const auto e = base64UrlDecode(key.value("e", ""));
        if (!n || !e)
            continue;
        if (EVP_PKEY* parsed = rsaKey(*n, *e))
            keys[key.value("kid", "")] = parsed;
    }
    if (keys.empty())
        return 0;
    for (auto& [kid, key] : m_keys)
        EVP_PKEY_free(static_cast<EVP_PKEY*>(key));
    m_keys = std::move(keys);
    return m_keys.size();
}

bool AccessJwt::refreshKeys()
{
    const std::int64_t now = m_now();
    if (m_fetched && now - m_fetched < KeysRefresh)
        return false;
    m_fetched = now;
    HttpRequest request;
    request.url = std::format("https://{}.cloudflareaccess.com/cdn-cgi/access/certs", m_team);
    const HttpResponse response = m_transport(request);
    if (response.status != 200 || !loadKeys(response.body)) {
        Log::info("access", "could not read the team's keys from {}: {}", request.url,
                  response.status ? std::to_string(response.status) : response.error);
        return false;
    }
    return true;
}

std::optional<std::string> AccessJwt::verify(const std::string& token, std::string* why)
{
    const auto refuse = [&](std::string reason) -> std::optional<std::string> {
        if (why)
            *why = std::move(reason);
        return std::nullopt;
    };
    const auto dot1 = token.find('.');
    const auto dot2 = dot1 == std::string::npos ? std::string::npos : token.find('.', dot1 + 1);
    if (dot2 == std::string::npos || token.find('.', dot2 + 1) != std::string::npos)
        return refuse("not a JWT");
    const auto headerText = base64UrlDecode(std::string_view(token).substr(0, dot1));
    const auto claimsText = base64UrlDecode(std::string_view(token).substr(dot1 + 1, dot2 - dot1 - 1));
    const auto signature = base64UrlDecode(std::string_view(token).substr(dot2 + 1));
    if (!headerText || !claimsText || !signature)
        return refuse("not base64url");
    const Json header = Json::parse(*headerText, nullptr, false);
    const Json claims = Json::parse(*claimsText, nullptr, false);
    if (!header.is_object() || !claims.is_object())
        return refuse("not JSON");
    if (header.value("alg", "") != "RS256")
        return refuse("not RS256");

    const std::string kid = header.value("kid", "");
    if (!m_keys.contains(kid))
        refreshKeys();
    const auto key = m_keys.find(kid);
    if (key == m_keys.end())
        return refuse("an unknown key");

    const std::string signedPart = token.substr(0, dot2);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context
        || EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, static_cast<EVP_PKEY*>(key->second)) != 1
        || EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature->data()), signature->size(),
                            reinterpret_cast<const unsigned char*>(signedPart.data()), signedPart.size())
            != 1)
        return refuse("a bad signature");

    const std::int64_t now = m_now();
    if (!claims.contains("exp") || !claims["exp"].is_number() || claims["exp"].get<std::int64_t>() + Leeway < now)
        return refuse("expired");
    if (claims.contains("nbf") && claims["nbf"].is_number() && claims["nbf"].get<std::int64_t>() - Leeway > now)
        return refuse("not yet valid");
    if (claims.value("iss", "") != std::format("https://{}.cloudflareaccess.com", m_team))
        return refuse("another issuer");
    bool audience = false;
    if (claims.contains("aud")) {
        const Json& aud = claims["aud"];
        if (aud.is_string())
            audience = aud.get<std::string>() == m_audience;
        else if (aud.is_array())
            audience = std::find(aud.begin(), aud.end(), m_audience) != aud.end();
    }
    if (!audience)
        return refuse("another application");
    const std::string email = claims.value("email", "");
    if (email.empty())
        return refuse("no email (a service token)");
    return email;
}

} // namespace Bots
