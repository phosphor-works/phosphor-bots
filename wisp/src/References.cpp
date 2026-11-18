// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "References.h"

#include <algorithm>
#include <cctype>

namespace Wisp {

namespace {

bool nameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
}

bool equalNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

/// @p text with code spans, code blocks and links blanked out, keeping each
/// other character where it was.
std::string withoutCode(std::string_view text)
{
    std::string out(text);
    std::size_t i = 0;
    while (i < out.size()) {
        if (out.compare(i, 3, "```") == 0) {
            const auto end = out.find("```", i + 3);
            const auto stop = end == std::string::npos ? out.size() : end + 3;
            std::fill(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(stop), ' ');
            i = stop;
        } else if (out[i] == '`') {
            const auto end = out.find('`', i + 1);
            const auto stop = end == std::string::npos ? out.size() : end + 1;
            std::fill(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(stop), ' ');
            i = stop;
        } else if (out.compare(i, 7, "http://") == 0 || out.compare(i, 8, "https://") == 0) {
            std::size_t stop = i;
            while (stop < out.size() && !std::isspace(static_cast<unsigned char>(out[stop])) && out[stop] != '>')
                ++stop;
            std::fill(out.begin() + std::ptrdiff_t(i), out.begin() + std::ptrdiff_t(stop), ' ');
            i = stop;
        } else {
            ++i;
        }
    }
    return out;
}

} // namespace

std::vector<Reference> findReferences(std::string_view message, const RepoResolver& resolve, std::string_view org,
                                      const std::optional<std::string>& bareRepo, std::size_t limit)
{
    std::vector<Reference> out;
    const std::string text = withoutCode(message);
    for (std::size_t hash = text.find('#'); hash != std::string::npos && out.size() < limit;
         hash = text.find('#', hash + 1)) {
        // The number: 1 to 7 digits, then no letter or digit.
        std::size_t end = hash + 1;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) && end - hash <= 7)
            ++end;
        if (end == hash + 1 || end - hash > 8 || (end < text.size() && nameChar(text[end]) && text[end] != '.'))
            continue;
        const int number = std::stoi(text.substr(hash + 1, end - hash - 1));
        if (number <= 0)
            continue;

        // The name before it, and an owner before a slash.
        std::size_t start = hash;
        while (start > 0 && nameChar(text[start - 1]))
            --start;
        std::string_view name(text.data() + start, hash - start);
        std::string_view owner;
        std::size_t wordStart = start;
        if (!name.empty() && start > 0 && text[start - 1] == '/') {
            std::size_t ownerStart = start - 1;
            while (ownerStart > 0 && nameChar(text[ownerStart - 1]))
                --ownerStart;
            owner = std::string_view(text.data() + ownerStart, start - 1 - ownerStart);
            wordStart = ownerStart;
        }
        // Glued to something else (a/b/c#1, x#y#2): no reference.
        if (wordStart > 0 && !std::isspace(static_cast<unsigned char>(text[wordStart - 1]))
            && std::string_view("([{<\"'*_~,;:!?").find(text[wordStart - 1]) == std::string_view::npos)
            continue;

        std::optional<std::string> repo;
        if (name.empty()) {
            repo = bareRepo;
        } else if (!owner.empty()) {
            if (equalNoCase(owner, org))
                repo = resolve(name);
        } else {
            repo = resolve(name);
        }
        if (!repo)
            continue;
        Reference reference{*repo, number};
        if (std::find(out.begin(), out.end(), reference) == out.end())
            out.push_back(std::move(reference));
    }
    return out;
}

} // namespace Wisp
