// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

/// What Glim writes on GitHub of a Discord post: a pure function of the
/// post's messages, so that what leaves Discord can be tested.
///
/// A GitHub issue is public; a Discord name is someone's. Each author is
/// written as "Reporter" (who opened the post) or "Member N", the same N
/// each time for one person; a mention of someone becomes their name here.
/// An "@word" cannot ping anyone on GitHub. Text files attached are put in
/// the issue (each cut to a limit), pictures and other files are named.
namespace Glim::Report {

struct Attachment
{
    std::string name;
    std::string contentType;
    long long size = 0;
    std::string url;
    std::optional<std::string> text; ///< a text file's bytes, once downloaded
};

struct Message
{
    Snowflake id = 0;
    Snowflake author = 0;
    std::string content;
    std::string timestamp; ///< ISO 8601, as Discord gives it
    std::vector<Attachment> attachments;
};

/// Who is who in a post, kept between updates.
struct Participants
{
    Snowflake reporter = 0;
    std::map<Snowflake, int> members;
    /// "Reporter", or "Member N" (a new N for someone new).
    std::string name(Snowflake user);
};

/// @p text cut to @p limit bytes, at a character's start.
std::string cut(std::string_view text, std::size_t limit);

/// Whether an attachment is text that the issue can hold: a text/ type, JSON,
/// or a log's or a configuration's name.
bool isText(const Attachment& attachment);

/// @p text with Discord's mentions written as names, and GitHub's mentions
/// made inert.
std::string anonymize(std::string_view text, Participants& participants);

struct Limits
{
    std::size_t text = 32 * 1024; ///< of each attached text file
    std::size_t total = 60000; ///< of the whole issue or comment (GitHub takes 65536)
};

/// A new issue's body: a line that says where it comes from, then the messages.
std::string issueBody(const std::string& postUrl, const std::vector<Message>& messages, Participants& participants,
                      Snowflake requester, const Limits& limits = {});
/// An update's comment: the messages since the last.
std::string updateComment(const std::string& postUrl, const std::vector<Message>& messages, Participants& participants,
                          Snowflake requester, const Limits& limits = {});

} // namespace Glim::Report
