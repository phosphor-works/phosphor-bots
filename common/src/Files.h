// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

namespace Bots {

/// The whole of @p path; nothing when it cannot be read (@p error says why,
/// empty when the file is missing).
std::optional<std::string> readFile(const std::string& path, std::string* error = nullptr);

/// Writes @p text to @p path through a new file (mode 600), fsync and a
/// rename, so that a crash leaves the old one whole. False, said in the log
/// under @p area, on failure.
bool writeFileAtomically(const std::string& path, const std::string& text, const char* area = "state");

} // namespace Bots
