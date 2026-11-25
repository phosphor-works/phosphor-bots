// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

/// What a direct post may not carry: it goes out with no one's check mark, so
/// a hit stops it, and the session sends a draft the owner sees instead.
namespace Nocti::Outbound {

struct Hit
{
    std::string rule; ///< "discord-token", "private-address", ...
    int line = 0; ///< 1-based
    int column = 0; ///< 1-based, in bytes
};

/// The first rule @p text hits, or nothing. @p ownToken: the bot's own token;
/// @p home: this machine's home folder, whose paths name its user.
std::vector<Hit> check(std::string_view text, std::string_view ownToken, std::string_view home);

} // namespace Nocti::Outbound
