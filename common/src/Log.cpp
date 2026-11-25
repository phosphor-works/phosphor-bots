// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Log.h"

#include <cstdio>
#include <mutex>
#include <string>

namespace Bots::Log {

namespace {
std::mutex s_mutex;
}

void line(std::string_view area, std::string_view text)
{
    // The gateway and the main thread both log: keep their lines whole.
    std::string out;
    out.reserve(area.size() + text.size() + 3);
    out.append(area).append(": ").append(text).push_back('\n');
    const std::lock_guard lock(s_mutex);
    std::fwrite(out.data(), 1, out.size(), stderr);
    std::fflush(stderr);
}

} // namespace Bots::Log
