// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Http.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Bots {

/// A GitHub App's access to one organization: a JWT signed with the app's
/// private key (RS256) is traded for an installation token, which lasts an
/// hour; a new one is fetched 10 minutes before it ends. What the bot writes
/// then shows as the app's (`name[bot]`), with only the permissions the app
/// was given.
class GitHubApp
{
public:
    using Now = std::function<std::int64_t()>; ///< Unix seconds

    /// @p privateKeyPem: the PEM the app's settings page made. Throws
    /// std::runtime_error when it is no RSA private key.
    GitHubApp(std::string appId, const std::string& privateKeyPem, std::string org, std::string userAgent,
              HttpTransport transport, Now now = {});
    ~GitHubApp();
    GitHubApp(const GitHubApp&) = delete;
    GitHubApp& operator=(const GitHubApp&) = delete;

    /// The installation token, fetched when there is none or it ends soon;
    /// empty when GitHub refused (said in the log).
    std::string token();

    /// The app's JWT at @p now, for the tests.
    std::string jwt(std::int64_t now) const;

private:
    std::string m_appId;
    void* m_key; ///< EVP_PKEY
    std::string m_org;
    std::string m_userAgent;
    HttpTransport m_transport;
    Now m_now;
    std::string m_installation;
    std::string m_token;
    std::int64_t m_tokenEnds = 0;
    std::string m_lastError;
};

/// Base64url without padding, as JWTs use.
std::string base64Url(std::string_view bytes);

} // namespace Bots
