// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common.h"

#include <map>
#include <set>
#include <string>

namespace Nocti {

/// Nocti's settings: a line each of its env file (~/.config/nocti/nocti.env,
/// mode 600). A key it does not know, a key set twice, an id that is no id or
/// a file others can read stops it, naming what is wrong.
struct Config
{
    std::string token; ///< NOCTI_TOKEN: the Discord bot's token
    Snowflake owner = 0; ///< NOCTI_OWNER: who approves each draft and lets a stranger through
    std::set<Snowflake> trusted; ///< NOCTI_TRUSTED: who starts trusted, until /nocti untrust
    std::set<Snowflake> channels; ///< NOCTI_CHANNELS: where it reads and posts; empty: every channel it sees
    bool direct = true; ///< NOCTI_DIRECT: 0 makes every post a draft
    int directMinutes = 30; ///< NOCTI_DIRECT_MINUTES: the interval of the posts that take one, 10 to 120
    int greetHours = 24; ///< NOCTI_GREET_HOURS: before a stranger is greeted again, 1 to 720
    int greetPerHour = 10; ///< NOCTI_GREET_PER_HOUR: greetings an hour, everyone together, 0 to 100
    std::string stateDir; ///< where its state goes (XDG_STATE_HOME/nocti)
    std::string cacheDir; ///< where attachments go (XDG_CACHE_HOME/nocti)
};

/// NAME=VALUE lines: blank lines and # comments skipped, values unquoted.
/// Throws std::runtime_error naming the line.
std::map<std::string, std::string> parseEnvText(const std::string& text);

/// The settings of @p values. Throws std::runtime_error naming the key.
Config parseConfig(const std::map<std::string, std::string>& values);

/// Reads @p path, refusing a file other users may read; fills the folders
/// from XDG_STATE_HOME, XDG_CACHE_HOME and HOME.
Config loadConfig(const std::string& path);

/// ~/.config/nocti/nocti.env (XDG_CONFIG_HOME honoured).
std::string defaultEnvPath();

} // namespace Nocti
