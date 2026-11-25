// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Outbound.h"

#include <regex>

namespace Nocti::Outbound {

namespace {

struct Rule
{
    const char* name;
    std::regex pattern;
};

const std::vector<Rule>& rules()
{
    static const auto flags = std::regex::ECMAScript;
    static const auto icase = std::regex::ECMAScript | std::regex::icase;
    static const std::vector<Rule> list = {
        {"discord-token", std::regex(R"([MNO][A-Za-z0-9_-]{23,27}\.[A-Za-z0-9_-]{6}\.[A-Za-z0-9_-]{27,40})", flags)},
        {"github-token", std::regex(R"(\b(gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}))", flags)},
        {"api-key",
         std::regex(
             R"(\b(sk-[A-Za-z0-9_-]{20,}|sk-ant-[A-Za-z0-9_-]{20,}|xox[abpr]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{30,}))",
             flags)},
        {"aws-key", std::regex(R"(\b(AKIA|ASIA)[0-9A-Z]{16}\b)", flags)},
        {"private-key", std::regex(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----)", flags)},
        {"secret-value",
         std::regex(R"(\b(key|token|secret|password|passwd)\s*[:=]\s*["']?[A-Za-z0-9+/_.-]{16,})", icase)},
        {"env-line", std::regex(R"(\b(NOCTI|WISP|GLIM)_[A-Z_]+=\S)", flags)},
        {"private-address",
         std::regex(
             R"(\b(10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}|127\.\d{1,3}\.\d{1,3}\.\d{1,3}|169\.254\.\d{1,3}\.\d{1,3}|fd[0-9a-f]{2}:[0-9a-f:]+)\b)",
             icase)},
        {"email", std::regex(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,})", flags)},
    };
    return list;
}

Hit at(std::string_view text, std::size_t offset, const char* rule)
{
    Hit hit{rule, 1, 1};
    for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++hit.line;
            hit.column = 1;
        } else {
            ++hit.column;
        }
    }
    return hit;
}

} // namespace

std::vector<Hit> check(std::string_view text, std::string_view ownToken, std::string_view home)
{
    std::vector<Hit> hits;
    // A token is long; a short value would match half of every text.
    if (ownToken.size() >= 20) {
        if (const auto found = text.find(ownToken); found != std::string_view::npos)
            hits.push_back(at(text, found, "own-token"));
    }
    if (home.size() > 1) {
        if (const auto found = text.find(home); found != std::string_view::npos)
            hits.push_back(at(text, found, "local-path"));
    }
    const std::string whole(text);
    for (const auto& rule : rules()) {
        std::smatch match;
        if (std::regex_search(whole, match, rule.pattern))
            hits.push_back(at(text, std::size_t(match.position(0)), rule.name));
    }
    return hits;
}

} // namespace Nocti::Outbound
