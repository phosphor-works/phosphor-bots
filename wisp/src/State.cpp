// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "State.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>

namespace Wisp {

using Json = nlohmann::json;

namespace {

constexpr int Format = 1;

std::string id(Snowflake value)
{
    return std::to_string(value);
}

Snowflake snowflake(const Json& json, const char* key)
{
    if (!json.contains(key))
        return 0;
    const auto& value = json[key];
    if (value.is_string())
        return parseSnowflake(value.get<std::string>()).value_or(0);
    return value.is_number_unsigned() ? value.get<Snowflake>() : 0;
}

} // namespace

Json toJson(const State& state)
{
    Json cards = Json::object();
    for (const auto& [key, card] : state.cards) {
        cards[key] = {{"channel", id(card.channel)}, {"message", id(card.message)}, {"drawn", card.drawn},
                      {"activity", card.activity},   {"posted", card.postedAt},     {"closed", card.closedAt}};
    }
    Json replies = Json::array();
    for (const auto& reply : state.replies) {
        Json references = Json::array();
        for (const auto& reference : reply.references)
            references.push_back({{"repo", reference.repo}, {"number", reference.number}});
        replies.push_back({{"channel", id(reply.channel)},
                           {"message", id(reply.message)},
                           {"source", id(reply.source)},
                           {"references", references},
                           {"drawn", reply.drawn},
                           {"at", reply.at}});
    }
    Json repos = Json::object();
    for (const auto& [name, repo] : state.repos)
        repos[name] = {{"head", repo.head}, {"releases", repo.releases}};
    Json comments = Json::object();
    for (const auto& [key, comment] : state.latestComments) {
        comments[key] = {{"issue", comment.issue},
                         {"author", comment.author},
                         {"body", comment.body},
                         {"url", comment.url},
                         {"created", comment.createdAt}};
    }
    return {{"format", Format}, {"cards", cards}, {"replies", replies}, {"repos", repos}, {"comments", comments}};
}

State stateFromJson(const Json& json)
{
    State state;
    if (!json.is_object() || json.value("format", 0) != Format)
        return state;
    if (json.contains("cards") && json["cards"].is_object()) {
        for (const auto& [key, card] : json["cards"].items()) {
            state.cards[key] = {snowflake(card, "channel"),
                                snowflake(card, "message"),
                                card.value("drawn", ""),
                                card.value("activity", ""),
                                card.value("posted", std::int64_t(0)),
                                card.value("closed", std::int64_t(0))};
        }
    }
    if (json.contains("replies") && json["replies"].is_array()) {
        for (const auto& reply : json["replies"]) {
            ReplyRecord record{snowflake(reply, "channel"), snowflake(reply, "message"),
                               snowflake(reply, "source"),  {},
                               reply.value("drawn", ""),    reply.value("at", std::int64_t(0))};
            if (reply.contains("references") && reply["references"].is_array()) {
                for (const auto& reference : reply["references"])
                    record.references.push_back({reference.value("repo", ""), reference.value("number", 0)});
            }
            state.replies.push_back(std::move(record));
        }
    }
    if (json.contains("repos") && json["repos"].is_object()) {
        for (const auto& [name, repo] : json["repos"].items()) {
            RepoRecord record{repo.value("head", ""), {}};
            if (repo.contains("releases") && repo["releases"].is_array()) {
                for (const auto& release : repo["releases"]) {
                    if (release.is_number_integer())
                        record.releases.push_back(release.get<long long>());
                }
            }
            state.repos[name] = std::move(record);
        }
    }
    if (json.contains("comments") && json["comments"].is_object()) {
        for (const auto& [key, comment] : json["comments"].items()) {
            state.latestComments[key] = {comment.value("issue", 0), comment.value("author", ""),
                                         comment.value("body", ""), comment.value("url", ""),
                                         comment.value("created", "")};
        }
    }
    return state;
}

std::optional<State> loadState(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        if (errno != ENOENT)
            Log::info("state", "{}: {}; starting with nothing kept", path, std::strerror(errno));
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    const Json json = Json::parse(text.str(), nullptr, false);
    if (!json.is_object() || json.value("format", 0) != Format) {
        Log::info("state", "{}: not a state this version reads; starting with nothing kept", path);
        return std::nullopt;
    }
    return stateFromJson(json);
}

bool saveState(const std::string& path, const State& state)
{
    const std::string temporary = path + ".new";
    const std::string text = toJson(state).dump() + "\n";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        Log::info("state", "{}: {}", temporary, std::strerror(errno));
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            Log::info("state", "{}: {}", temporary, std::strerror(errno));
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written += std::size_t(n);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0 || std::rename(temporary.c_str(), path.c_str()) != 0) {
        Log::info("state", "{}: {}", path, std::strerror(errno));
        ::unlink(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace Wisp
