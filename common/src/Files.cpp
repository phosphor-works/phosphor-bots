// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Files.h"

#include "Log.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>

namespace Bots {

std::optional<std::string> readFile(const std::string& path, std::string* error)
{
    errno = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error)
            *error = errno == ENOENT ? std::string() : std::string(std::strerror(errno));
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

bool writeFileAtomically(const std::string& path, const std::string& text, const char* area)
{
    const std::string temporary = path + ".new";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        Log::info(area, "{}: {}", temporary, std::strerror(errno));
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            Log::info(area, "{}: {}", temporary, std::strerror(errno));
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written += std::size_t(n);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0 || std::rename(temporary.c_str(), path.c_str()) != 0) {
        Log::info(area, "{}: {}", path, std::strerror(errno));
        ::unlink(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace Bots
