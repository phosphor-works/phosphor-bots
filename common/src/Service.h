// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

namespace Bots {

/// Keeps the process's memory and environment, where the tokens are, from
/// debuggers of its own user: PROC_TRACE_CTL on FreeBSD, not dumpable on
/// Linux. False (errno set) when the kernel refused.
bool hideFromDebuggers();

/// Runs @p run on this thread while another waits for SIGTERM, SIGINT or
/// SIGHUP and then calls @p stop. Returns what @p run returned. Call it before
/// any other thread starts: the signals are blocked for every thread made
/// after.
int runUntilSignalled(const char* name, const std::function<int()>& run, const std::function<void()>& stop);

} // namespace Bots
