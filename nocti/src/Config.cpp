// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

#include "Files.h"

#include <charconv>
#include <cstdlib>
#include <format>
#include <stdexcept>

#include <sys/stat.h>

namespace Nocti {

namespace {

const std::set<std::string> KnownKeys = {"NOCTI_TOKEN",    "NOCTI_OWNER",  "NOCTI_TRUSTED",
                                         "NOCTI_CHANNELS", "NOCTI_DIRECT", "NOCTI_DIRECT_MINUTES"};

[[noreturn]] void fail(const std::string& what)
{
    throw std::runtime_error(what);
}

std::string trim(std::string text)
{
    text.erase(0, text.find_first_not_of(" \t\r"));
    text.erase(text.find_last_not_of(" \t\r") + 1);
    return text;
}

std::set<Snowflake> ids(const std::string& key, const std::string& value)
{
    std::set<Snowflake> out;
    std::size_t at = 0;
    while (at <= value.size()) {
        const auto comma = value.find(',', at);
        const std::string word = trim(value.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (!word.empty()) {
            const auto id = parseSnowflake(word);
            if (!id)
                fail(std::format("{}: \"{}\" is not an id (Developer Mode, right click, Copy ID)", key, word));
            out.insert(*id);
        }
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    return out;
}

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string home()
{
    const std::string value = environment("HOME");
    if (value.empty())
        fail("HOME is not set");
    return value;
}

} // namespace

std::map<std::string, std::string> parseEnvText(const std::string& text)
{
    std::map<std::string, std::string> out;
    std::size_t at = 0;
    int number = 0;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        const std::string line = trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() : end + 1;
        ++number;
        if (line.empty() || line.front() == '#')
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0)
            fail(std::format("line {}: not NAME=VALUE", number));
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        if (out.contains(key))
            fail(std::format("line {}: {} is set twice", number, key));
        out[key] = value;
    }
    return out;
}

Config parseConfig(const std::map<std::string, std::string>& values)
{
    for (const auto& [key, value] : values) {
        if (!KnownKeys.contains(key))
            fail(std::format("{}: no such setting", key));
    }
    const auto get = [&](const char* key) {
        const auto it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    };
    Config config;
    config.token = get("NOCTI_TOKEN");
    if (config.token.empty())
        fail("NOCTI_TOKEN: needed, the Discord bot's token");
    const auto owner = parseSnowflake(get("NOCTI_OWNER"));
    if (!owner)
        fail("NOCTI_OWNER: needed, the owner's Discord user id");
    config.owner = *owner;
    config.trusted = ids("NOCTI_TRUSTED", get("NOCTI_TRUSTED"));
    config.trusted.erase(config.owner);
    config.channels = ids("NOCTI_CHANNELS", get("NOCTI_CHANNELS"));
    if (const auto direct = get("NOCTI_DIRECT"); !direct.empty()) {
        if (direct != "0" && direct != "1")
            fail(std::format("NOCTI_DIRECT: \"{}\" is not 0 or 1", direct));
        config.direct = direct == "1";
    }
    if (const auto minutes = get("NOCTI_DIRECT_MINUTES"); !minutes.empty()) {
        int value = 0;
        const auto [end, ec] = std::from_chars(minutes.data(), minutes.data() + minutes.size(), value);
        if (ec != std::errc{} || end != minutes.data() + minutes.size() || value < 10 || value > 120)
            fail(std::format("NOCTI_DIRECT_MINUTES: \"{}\" is not a number from 10 to 120", minutes));
        config.directMinutes = value;
    }
    return config;
}

std::string defaultEnvPath()
{
    const std::string base = environment("XDG_CONFIG_HOME");
    return (base.empty() ? home() + "/.config" : base) + "/nocti/nocti.env";
}

Config loadConfig(const std::string& path)
{
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0)
        fail(std::format("{}: no such file (see docs/NOCTI.md, Install)", path));
    if (status.st_mode & 077)
        fail(std::format("{}: mode {:o}, which other users may read; chmod 600 it", path, status.st_mode & 0777));
    std::string error;
    const auto text = readFile(path, &error);
    if (!text)
        fail(std::format("{}: {}", path, error));
    Config config;
    try {
        config = parseConfig(parseEnvText(*text));
    } catch (const std::runtime_error& problem) {
        fail(std::format("{}: {}", path, problem.what()));
    }
    const std::string state = environment("XDG_STATE_HOME");
    const std::string cache = environment("XDG_CACHE_HOME");
    config.stateDir = (state.empty() ? home() + "/.local/state" : state) + "/nocti";
    config.cacheDir = (cache.empty() ? home() + "/.cache" : cache) + "/nocti";
    return config;
}

} // namespace Nocti
