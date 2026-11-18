// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Wisp {

struct Reference
{
    std::string repo; ///< the repository's own name, as GitHub spells it
    int number = 0;
    bool operator==(const Reference&) const = default;
};

/// A repository's own name for a name or an alias someone wrote, in any case;
/// nothing for a name that is none of the organization's.
using RepoResolver = std::function<std::optional<std::string>(std::string_view)>;

/// The issues and pull requests a message names, in order, each once, at most
/// @p limit:
///
/// - `Repo#123` and `alias#123`, a repository or alias the resolver knows;
/// - `org/Repo#123`, with the organization @p org;
/// - `#123` alone, the repository @p bareRepo (none: a bare number is talk).
///
/// Code (`...` and ```...```) and links are passed over: a number in them is
/// no question.
std::vector<Reference> findReferences(std::string_view text, const RepoResolver& resolve, std::string_view org,
                                      const std::optional<std::string>& bareRepo, std::size_t limit);

} // namespace Wisp
