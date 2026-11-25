// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Preharness.h"

#include <regex>
#include <vector>

namespace Nocti::Preharness {

namespace {

std::vector<char32_t> decode(std::string_view text)
{
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + std::size_t(length) > text.size())
            length = 1;
        char32_t point = length == 1 ? c : c & (0xFF >> (length + 1));
        for (int k = 1; k < length; ++k)
            point = (point << 6) | (static_cast<unsigned char>(text[i + std::size_t(k)]) & 0x3F);
        out.push_back(point);
        i += std::size_t(length);
    }
    return out;
}

bool emoji(char32_t c)
{
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || c == 0xFE0F
        || (c >= 0x1F3FB && c <= 0x1F3FF);
}

struct Rule
{
    const char* name;
    const char* why;
    std::regex pattern;
};

const std::vector<Rule>& cautionRules()
{
    static const auto icase = std::regex::icase | std::regex::ECMAScript;
    static const std::vector<Rule> rules = {
        {"instructions", "words that try to change the session's instructions",
         std::regex(
             R"((ignore|disregard|forget)\b.{0,30}\b(instruction|rule|prompt)s?|system prompt|you are now|new instructions|developer mode)",
             icase)},
        {"secrets", "asks for or names a secret",
         std::regex(R"(\b(token|password|passwd|api[ _-]?key|secret|private key|ssh key|credential)s?\b|\.env\b)",
                    icase)},
        {"destructive", "a destructive or publishing command",
         std::regex(
             R"(rm\s+-rf|drop\s+table|reset\s+--hard|force[ -]?push|push\s+--force|\bgit\s+push\b|\bmerge\b|delete\s+(the\s+)?(repo|branch|database))",
             icase)},
        {"owner-claim", "claims the owner's word",
         std::regex(R"(\b(fuddles(worth)?|nate|the owner)\b.{0,20}\b(said|says|told|approved|wants|asked)\b)", icase)},
    };
    return rules;
}

} // namespace

char32_t hiddenLetter(std::string_view text)
{
    const auto points = decode(text);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const char32_t c = points[i];
        const bool zeroWidth =
            (c >= 0x200B && c <= 0x200F) || (c >= 0x2060 && c <= 0x2064) || c == 0xFEFF || c == 0x180E;
        const bool bidi = (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
        const bool tag = c >= 0xE0000 && c <= 0xE007F;
        if (!zeroWidth && !bidi && !tag)
            continue;
        // The joiner inside an emoji sequence, and a flag's tags after its
        // black flag, are part of the emoji.
        if (c == 0x200D && i > 0 && i + 1 < points.size() && emoji(points[i - 1]) && emoji(points[i + 1]))
            continue;
        if (tag) {
            std::size_t start = i;
            while (start > 0 && points[start - 1] >= 0xE0000 && points[start - 1] <= 0xE007F)
                --start;
            if (start > 0 && points[start - 1] == 0x1F3F4)
                continue;
        }
        return c;
    }
    return 0;
}

Verdict inbound(std::string_view text, bool fromOwner)
{
    Verdict verdict;
    if (const char32_t hidden = hiddenLetter(text)) {
        char code[16];
        std::snprintf(code, sizeof code, "U+%04X", unsigned(hidden));
        if (fromOwner)
            verdict.cautions.push_back(std::string("hidden-letter: ") + code);
        else
            verdict.reject = std::string("hidden-letter (") + code + ")";
    }
    const std::string whole(text);
    for (const auto& rule : cautionRules()) {
        if (std::regex_search(whole, rule.pattern))
            verdict.cautions.push_back(std::string(rule.name) + ": " + rule.why);
    }
    return verdict;
}

} // namespace Nocti::Preharness
