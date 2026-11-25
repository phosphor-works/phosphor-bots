// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Brand.h"
#include "Common.h"

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

/// The messages Wisp posts, as Discord's message JSON: a pure function of
/// what GitHub says, so that a card is edited only when its drawing changes.
namespace Wisp::Embeds {

/// Each kind of message has its colour of the palette, so that a channel
/// tells them apart at a glance; the text and the CI icons say the state.
namespace KindColor {
inline constexpr int PullRequest = Brand::Blue;
inline constexpr int Issue = Brand::Cyan;
inline constexpr int Push = Brand::Purple;
inline constexpr int Release = Brand::Rose;
/// A draft, and an item closed or merged: nothing more to do there.
inline constexpr int Finished = Brand::Slate;
} // namespace KindColor

/// What every message carries: the organization it speaks for, with its
/// avatar (the Phosphor mark) above the title, and Wisp's avatar beside its
/// name in the footer. An icon without an address is left out.
struct Look
{
    std::string org;
    std::string iconUrl; ///< Wisp's
    std::string orgIconUrl; ///< the organization's
};

/// What a pull request's or an issue's card shows beside the item itself.
struct CardExtras
{
    std::vector<WorkflowRun> runs;
    std::optional<Comment> latestComment;
};

/// A push to a repository's default branch.
struct Push
{
    std::string repo;
    std::string branch;
    std::string before; ///< empty: the first head seen
    Commit head;
    std::vector<Commit> commits; ///< oldest first
    int total = 0;
    std::vector<WorkflowRun> runs;
};

/// One reply for the items a message named (an embed each).
nlohmann::json reply(const Look& look, const std::vector<Item>& items);
nlohmann::json itemCard(const Look& look, const Item& item, const CardExtras& extras);
nlohmann::json pushCard(const Look& look, const Push& push);
nlohmann::json releaseCard(const Look& look, const std::string& repo, const Release& release);

/// The colour of an item: its kind's while it is open, Finished once it is a
/// draft, closed or merged.
int itemColor(const Item& item);
/// The words for an item's state: "open pull request", "merged pull request"...
std::string itemState(const Item& item);
/// One line of the runs' results, each linked: "✅ Arch · ❌ Lint · ⏳ Nix".
std::string runsLine(const std::vector<WorkflowRun>& runs);
/// The combined result: success, failure, pending, or empty with no runs.
std::string runsResult(const std::vector<WorkflowRun>& runs);
/// @p text with Discord's markdown characters escaped.
std::string escape(std::string_view text);
/// @p text cut to @p limit bytes (at a character's start), with an ellipsis.
std::string shorten(std::string_view text, std::size_t limit);

} // namespace Wisp::Embeds
