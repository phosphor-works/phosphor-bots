// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "State.h"

#include "Files.h"

namespace Glim {

using Json = nlohmann::json;

namespace {

constexpr int Format = 1;

Snowflake snowflake(const Json& json, const char* key)
{
    return json.contains(key) && json[key].is_string() ? parseSnowflake(json[key].get<std::string>()).value_or(0) : 0;
}

} // namespace

Json filesJson(const std::vector<KeptFile>& files, bool forPublic)
{
    Json out = Json::array();
    for (const auto& file : files) {
        if (forPublic && file.removed)
            continue;
        Json entry = {{"name", file.name}, {"kind", file.kind}, {"size", file.size}, {"url", file.url}};
        if (!forPublic) {
            entry["path"] = file.path;
            entry["removed"] = file.removed;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

Json toJson(const State& state)
{
    Json posts = Json::object();
    for (const auto& [id, post] : state.posts) {
        Json members = Json::object();
        for (const auto& [user, number] : post.participants.members)
            members[std::to_string(user)] = number;
        posts[std::to_string(id)] = {{"repo", post.repo},
                                     {"number", post.number},
                                     {"url", post.url},
                                     {"last", std::to_string(post.lastMessage)},
                                     {"reporter", std::to_string(post.participants.reporter)},
                                     {"members", members},
                                     {"state", post.issueState},
                                     {"title", post.title},
                                     {"filed", post.filedAt},
                                     {"updated", post.updatedAt},
                                     {"discord", post.discordUrl},
                                     {"files", filesJson(post.files)}};
    }
    return {{"format", Format}, {"posts", posts}};
}

State stateFromJson(const Json& json)
{
    State state;
    if (!json.is_object() || json.value("format", 0) != Format || !json.contains("posts"))
        return state;
    for (const auto& [id, post] : json["posts"].items()) {
        const auto thread = parseSnowflake(id);
        if (!thread)
            continue;
        PostRecord record;
        record.repo = post.value("repo", "");
        record.number = post.value("number", 0);
        record.url = post.value("url", "");
        record.lastMessage = snowflake(post, "last");
        record.participants.reporter = snowflake(post, "reporter");
        if (post.contains("members") && post["members"].is_object()) {
            for (const auto& [user, number] : post["members"].items()) {
                if (const auto parsed = parseSnowflake(user); parsed && number.is_number_integer())
                    record.participants.members[*parsed] = number.get<int>();
            }
        }
        record.issueState = post.value("state", "open");
        record.title = post.value("title", "");
        record.filedAt = post.value("filed", std::int64_t(0));
        record.updatedAt = post.value("updated", std::int64_t(0));
        record.discordUrl = post.value("discord", "");
        if (post.contains("files") && post["files"].is_array()) {
            for (const auto& file : post["files"]) {
                record.files.push_back({file.value("name", ""), file.value("kind", ""), file.value("size", 0LL),
                                        file.value("url", ""), file.value("path", ""), file.value("removed", false)});
            }
        }
        state.posts[*thread] = std::move(record);
    }
    return state;
}

std::optional<State> loadState(const std::string& path)
{
    std::string error;
    const auto text = readFile(path, &error);
    if (!text) {
        if (!error.empty())
            Log::info("state", "{}: {}; starting with nothing kept", path, error);
        return std::nullopt;
    }
    const Json json = Json::parse(*text, nullptr, false);
    if (!json.is_object() || json.value("format", 0) != Format) {
        Log::info("state", "{}: not a state this version reads; starting with nothing kept", path);
        return std::nullopt;
    }
    return stateFromJson(json);
}

bool saveState(const std::string& path, const State& state)
{
    return writeFileAtomically(path, toJson(state).dump() + "\n");
}

} // namespace Glim
