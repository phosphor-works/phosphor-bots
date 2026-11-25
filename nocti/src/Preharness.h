// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

/// The rules a message passes before it reaches the session. Hidden letters
/// (zero-width, bidi overrides, Unicode tags; the joiners inside an emoji
/// pass) reject someone else's message: the session never gets its text.
/// The rest add a caution the session sees beside the text.
namespace Nocti::Preharness {

struct Verdict
{
    std::string reject; ///< the rule that rejects it; empty: it may pass
    std::vector<std::string> cautions; ///< each rule it hit, with why
};

/// @p fromOwner: the owner's message is never rejected; what would reject
/// another's only adds a caution.
Verdict inbound(std::string_view text, bool fromOwner);

/// The code point of a hidden letter in @p text, or 0.
char32_t hiddenLetter(std::string_view text);

} // namespace Nocti::Preharness
