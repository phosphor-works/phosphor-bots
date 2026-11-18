// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>

extern char** environ;

namespace Glim {

namespace {

const std::set<std::string, std::less<>> KnownKeys = {
    "GLIM_TOKEN",       "GLIM_APP_ID",      "GLIM_APP_KEY",      "GLIM_ORG",      "GLIM_FORUMS",   "GLIM_TAG_REPOS",
    "GLIM_ALIASES",     "GLIM_STAFF_ROLES", "GLIM_SOLVED_TAG",   "GLIM_LABELS",   "GLIM_STATE",    "GLIM_POLL_SECONDS",
    "GLIM_TEXT_LIMIT",  "GLIM_FILES_DIR",   "GLIM_FILES_URL",    "GLIM_FILE_MIB", "GLIM_POST_MIB", "GLIM_STAFF_LISTEN",
    "GLIM_ACCESS_TEAM", "GLIM_ACCESS_AUD",  "GLIM_STAFF_EMAILS",
};

[[noreturn]] void fail(std::string_view key, std::string_view why)
{
    throw std::runtime_error(std::format("{}: {}", key, why));
}

std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '\t'))
            ++i;
        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != ',' && text[i] != '\t')
            ++i;
        if (i > start)
            out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

bool name(std::string_view text)
{
    return !text.empty() && text.size() <= 100 && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
    });
}

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return out;
}

Snowflake id(std::string_view key, std::string_view text)
{
    const auto value = parseSnowflake(text);
    if (!value)
        fail(key, std::format("\"{}\" is not an id (Developer Mode, right click, Copy ID)", text));
    return *value;
}

long number(std::string_view key, std::string_view text, long low, long high)
{
    long value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < low || value > high)
        fail(key, std::format("\"{}\" is not a number from {} to {}", text, low, high));
    return value;
}

std::map<std::string, std::string> pairs(std::string_view key, std::string_view text)
{
    std::map<std::string, std::string> out;
    for (const auto& pair : words(text)) {
        const auto equals = pair.find('=');
        if (equals == std::string::npos || !name(pair.substr(0, equals)) || !name(pair.substr(equals + 1)))
            fail(key, std::format("\"{}\" is not name=Repository", pair));
        out[lower(pair.substr(0, equals))] = pair.substr(equals + 1);
    }
    return out;
}

} // namespace

std::map<std::string, std::string> processEnvironment()
{
    std::map<std::string, std::string> out;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view text(*entry);
        if (const auto equals = text.find('='); equals != std::string_view::npos)
            out.emplace(std::string(text.substr(0, equals)), std::string(text.substr(equals + 1)));
    }
    return out;
}

Config parseConfig(const std::map<std::string, std::string>& env)
{
    for (const auto& [key, value] : env) {
        if (key.starts_with("GLIM_") && !KnownKeys.contains(key))
            fail(key, "no such setting");
    }
    const auto get = [&](std::string_view key) {
        const auto it = env.find(std::string(key));
        return it == env.end() ? std::string() : it->second;
    };
    Config config;
    config.token = get("GLIM_TOKEN");
    if (config.token.empty())
        fail("GLIM_TOKEN", "needed: the Discord bot's token");
    config.appId = get("GLIM_APP_ID");
    if (config.appId.empty() || !std::all_of(config.appId.begin(), config.appId.end(), ::isdigit))
        fail("GLIM_APP_ID", "needed: the GitHub App's id, a number (its settings page, About)");
    config.appKeyPath = get("GLIM_APP_KEY");
    if (config.appKeyPath.empty() || config.appKeyPath.front() != '/')
        fail("GLIM_APP_KEY", "needed: the absolute path of the GitHub App's private key (.pem)");
    if (const auto org = get("GLIM_ORG"); !org.empty()) {
        if (!name(org))
            fail("GLIM_ORG", std::format("\"{}\" is not an organization's name", org));
        config.org = org;
    }
    for (const auto& forum : words(get("GLIM_FORUMS")))
        config.forums.insert(id("GLIM_FORUMS", forum));
    if (config.forums.empty())
        fail("GLIM_FORUMS", "needed: the forum channel ids whose posts it files (#help)");
    config.tagRepos = pairs("GLIM_TAG_REPOS", get("GLIM_TAG_REPOS"));
    config.aliases = pairs("GLIM_ALIASES", get("GLIM_ALIASES"));
    for (const auto& role : words(get("GLIM_STAFF_ROLES")))
        config.staffRoles.insert(id("GLIM_STAFF_ROLES", role));
    if (env.contains("GLIM_SOLVED_TAG"))
        config.solvedTag = lower(get("GLIM_SOLVED_TAG"));
    config.labels = words(get("GLIM_LABELS"));
    config.statePath = get("GLIM_STATE");
    if (const auto value = get("GLIM_POLL_SECONDS"); !value.empty())
        config.pollInterval = std::chrono::seconds(number("GLIM_POLL_SECONDS", value, 60, 3600));
    if (const auto value = get("GLIM_TEXT_LIMIT"); !value.empty())
        config.textLimit = std::size_t(number("GLIM_TEXT_LIMIT", value, 1024, 60000));
    config.filesDir = get("GLIM_FILES_DIR");
    config.filesUrl = get("GLIM_FILES_URL");
    while (config.filesUrl.ends_with('/'))
        config.filesUrl.pop_back();
    if (!config.filesDir.empty() && config.filesDir.front() != '/')
        fail("GLIM_FILES_DIR", "an absolute path");
    if (config.filesDir.empty() != config.filesUrl.empty())
        fail(config.filesDir.empty() ? "GLIM_FILES_DIR" : "GLIM_FILES_URL",
             "GLIM_FILES_DIR and GLIM_FILES_URL go together");
    if (!config.filesUrl.empty() && !config.filesUrl.starts_with("https://"))
        fail("GLIM_FILES_URL", "an address that starts with https://");
    if (const auto value = get("GLIM_FILE_MIB"); !value.empty())
        config.fileLimit = std::size_t(number("GLIM_FILE_MIB", value, 1, 500)) << 20;
    if (const auto value = get("GLIM_POST_MIB"); !value.empty())
        config.postLimit = std::size_t(number("GLIM_POST_MIB", value, 1, 2000)) << 20;
    config.staffListen = get("GLIM_STAFF_LISTEN");
    config.accessTeam = get("GLIM_ACCESS_TEAM");
    config.accessAud = get("GLIM_ACCESS_AUD");
    for (const auto& email : words(get("GLIM_STAFF_EMAILS")))
        config.staffEmails.insert(lower(email));
    if (!config.staffListen.empty()) {
        if (config.staffListen.find(':') == std::string::npos)
            fail("GLIM_STAFF_LISTEN", "an address and a port: 10.17.0.7:8090");
        if (config.accessTeam.empty() || !name(config.accessTeam))
            fail("GLIM_ACCESS_TEAM", "needed by GLIM_STAFF_LISTEN: the Zero Trust team's name");
        if (config.accessAud.empty())
            fail("GLIM_ACCESS_AUD", "needed by GLIM_STAFF_LISTEN: the Access application's AUD tag");
        if (config.filesDir.empty())
            fail("GLIM_FILES_DIR", "needed by GLIM_STAFF_LISTEN: the issues page reads it");
    }
    return config;
}

} // namespace Glim
