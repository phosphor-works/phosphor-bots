// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Http.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace Bots {

/// Checks the token Cloudflare Access puts on each request it lets through
/// (the Cf-Access-Jwt-Assertion header): an RS256 JWT signed with one of the
/// team's keys, for one application (its AUD tag), not expired. What sits
/// behind Access trusts the login only through this check, so a request that
/// reaches it some other way is refused.
class AccessJwt
{
public:
    using Now = std::function<std::int64_t()>; ///< Unix seconds

    /// @p team: the Zero Trust team's name (<team>.cloudflareaccess.com);
    /// @p audience: the application's AUD tag.
    AccessJwt(std::string team, std::string audience, HttpTransport transport, Now now = {});
    ~AccessJwt();
    AccessJwt(const AccessJwt&) = delete;
    AccessJwt& operator=(const AccessJwt&) = delete;

    /// The email of the person the token is for; nothing when the token is no
    /// good (@p why says why).
    std::optional<std::string> verify(const std::string& token, std::string* why = nullptr);

    /// The team's keys as Cloudflare publishes them (a JWKS document); the
    /// tests give theirs. Returns how many RSA keys it took.
    std::size_t loadKeys(const std::string& jwks);

private:
    bool refreshKeys();

    std::string m_team;
    std::string m_audience;
    HttpTransport m_transport;
    Now m_now;
    std::map<std::string, void*> m_keys; ///< kid -> EVP_PKEY
    std::int64_t m_fetched = 0;
};

/// Base64url (with or without padding) to bytes; nothing when it is not that.
std::optional<std::string> base64UrlDecode(std::string_view text);

} // namespace Bots
