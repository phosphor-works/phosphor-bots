// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <format>
#include <string_view>

/// One line on stderr for each event: daemon(8) sends it to syslog under the
/// service's tag, so the line carries no time of its own.
namespace Bots::Log {

void line(std::string_view area, std::string_view text);

template<typename... Args>
void info(std::string_view area, std::format_string<Args...> format, Args&&... args)
{
    line(area, std::format(format, std::forward<Args>(args)...));
}

} // namespace Bots::Log
