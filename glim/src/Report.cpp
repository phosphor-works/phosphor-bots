// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Report.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Glim::Report {

namespace {

constexpr std::string_view ZeroWidthSpace = "​";

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return out;
}

bool endsWith(std::string_view text, std::string_view end)
{
    return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

/// A code fence longer than any run of backticks in @p text.
std::string fenceFor(std::string_view text)
{
    std::size_t longest = 0, run = 0;
    for (const char c : text) {
        run = c == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return std::string(std::max<std::size_t>(3, longest + 1), '`');
}

std::string size(long long bytes)
{
    if (bytes >= 1024 * 1024)
        return std::format("{:.1f} MiB", double(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return std::format("{:.0f} KiB", double(bytes) / 1024.0);
    return std::format("{} B", bytes);
}

/// "2026-11-18T14:05:31.123000+00:00" -> "2026-11-18 14:05 UTC"
std::string when(const std::string& timestamp)
{
    if (timestamp.size() < 16)
        return {};
    return timestamp.substr(0, 10) + " " + timestamp.substr(11, 5) + " UTC";
}

std::string messageText(const Message& message, Participants& participants, const Limits& limits)
{
    std::string out = std::format("#### {} · {}\n\n", participants.name(message.author), when(message.timestamp));
    if (const std::string text = anonymize(message.content, participants); !text.empty())
        out += text + "\n\n";
    std::vector<std::string> others;
    for (const auto& attachment : message.attachments) {
        const std::string name = anonymize(attachment.name, participants);
        if (attachment.text) {
            const std::string body = cut(*attachment.text, limits.text);
            const std::string fence = fenceFor(body);
            const std::string whole = attachment.publicUrl.empty()
                ? std::string()
                : std::format("📄 [The whole file]({})\n\n", attachment.publicUrl);
            out += std::format("<details><summary>📄 {} ({}){}</summary>\n\n{}text\n{}\n{}\n\n</details>\n\n{}", name,
                               size(attachment.size), body.size() < attachment.text->size() ? ", cut" : "", fence, body,
                               fence, whole);
        } else if (attachment.kind == "image") {
            out += std::format("![{}]({})\n\n", name, attachment.publicUrl);
        } else if (attachment.kind == "video") {
            out += std::format("🎬 [{}]({}) ({})\n\n", name, attachment.publicUrl, size(attachment.size));
        } else if (!attachment.publicUrl.empty()) {
            out += std::format("📦 [{}]({}) ({})\n\n", name, attachment.publicUrl, size(attachment.size));
        } else {
            others.push_back(name);
        }
    }
    if (!others.empty()) {
        out += "📎 Attached in the Discord post: ";
        for (std::size_t i = 0; i < others.size(); ++i)
            out += (i ? ", " : "") + std::string("`") + others[i] + "`";
        out += "\n\n";
    }
    return out;
}

std::string transcript(const std::vector<Message>& messages, Participants& participants, const Limits& limits,
                       std::size_t room, std::string_view postUrl)
{
    std::string out;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        std::string part = messageText(messages[i], participants, limits);
        if (out.size() + part.size() > room) {
            out += std::format("_And {} more messages in the [Discord post]({})._\n", messages.size() - i, postUrl);
            break;
        }
        out += part;
    }
    return out;
}

} // namespace

std::string cut(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string(text);
    std::size_t at = limit;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
        --at;
    return std::string(text.substr(0, at));
}

std::string Participants::name(Snowflake user)
{
    if (user == reporter)
        return "Reporter";
    auto it = members.find(user);
    if (it == members.end()) {
        int next = 1;
        for (const auto& [id, number] : members)
            next = std::max(next, number + 1);
        it = members.emplace(user, next).first;
    }
    return std::format("Member {}", it->second);
}

bool isText(const Attachment& attachment)
{
    const std::string type = lower(attachment.contentType);
    if (type.starts_with("text/") || type.starts_with("application/json") || type.starts_with("application/xml")
        || type.starts_with("application/x-yaml") || type.starts_with("application/toml"))
        return true;
    const std::string name = lower(attachment.name);
    for (const char* end : {".log", ".txt", ".json", ".conf", ".ini", ".toml", ".yaml", ".yml", ".xml", ".qml", ".js",
                            ".diff", ".patch", ".cfg"}) {
        if (endsWith(name, end))
            return true;
    }
    return false;
}

std::string anonymize(std::string_view text, Participants& participants)
{
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        // Discord's markup: <@id>, <@!id>, <@&id>, <#id>, <:name:id>, <a:name:id>
        if (text[i] == '<') {
            const auto close = text.find('>', i);
            if (close != std::string_view::npos && close - i < 64) {
                const std::string_view tag = text.substr(i + 1, close - i - 1);
                if (tag.starts_with("@&")) {
                    out += "@" + std::string(ZeroWidthSpace) + "role";
                    i = close + 1;
                    continue;
                }
                if (tag.starts_with("@")) {
                    const std::string_view digits = tag.substr(tag.starts_with("@!") ? 2 : 1);
                    if (const auto user = parseSnowflake(digits)) {
                        out += participants.name(*user);
                        i = close + 1;
                        continue;
                    }
                }
                if (tag.starts_with("#") && parseSnowflake(tag.substr(1))) {
                    out += "#channel";
                    i = close + 1;
                    continue;
                }
                if (tag.starts_with(":") || tag.starts_with("a:")) {
                    const std::string_view rest = tag.substr(tag.starts_with("a:") ? 2 : 1);
                    if (const auto colon = rest.find(':'); colon != std::string_view::npos) {
                        out += ":" + std::string(rest.substr(0, colon)) + ":";
                        i = close + 1;
                        continue;
                    }
                }
            }
        }
        // GitHub pings @login and @org/team: a zero-width space after the @
        // keeps the text and drops the ping.
        if (text[i] == '@' && i + 1 < text.size()
            && (std::isalnum(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '-')) {
            out += '@';
            out += ZeroWidthSpace;
            ++i;
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::string issueBody(const std::string& postUrl, const std::vector<Message>& messages, Participants& participants,
                      Snowflake requester, const Limits& limits)
{
    // The messages first, so that members are numbered as they speak; the
    // head names the requester after.
    const std::string body =
        transcript(messages, participants, limits, limits.total - std::min(limits.total, std::size_t(600)), postUrl);
    return std::format(
               "_Filed by Glim from a [post]({}) in the #help forum of the Phosphor Works Discord, at the "
               "request of {}. Discord names are not shown: Reporter opened the post._\n\n",
               postUrl, requester ? participants.name(requester) : std::string("staff, on the issues page"))
        + body;
}

std::string updateComment(const std::string& postUrl, const std::vector<Message>& messages, Participants& participants,
                          Snowflake requester, const Limits& limits)
{
    const std::string body =
        transcript(messages, participants, limits, limits.total - std::min(limits.total, std::size_t(600)), postUrl);
    return std::format("_New in the [Discord post]({}) since the last update, added by Glim at the request of {}._\n\n",
                       postUrl, requester ? participants.name(requester) : std::string("staff, on the issues page"))
        + body;
}

} // namespace Glim::Report
