// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "References.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

using namespace Wisp;

namespace {

std::optional<std::string> resolve(std::string_view name)
{
    static const std::map<std::string, std::string> known = {
        {"afterglow", "Afterglow"},
        {"plasmazones", "PlasmaZones"},
        {"pz", "PlasmaZones"},
        {"phosphor-libs", "phosphor-libs"},
    };
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    const auto it = known.find(key);
    return it == known.end() ? std::nullopt : std::optional(it->second);
}

std::vector<Reference> find(std::string_view text, std::optional<std::string> bare = std::nullopt,
                            std::size_t limit = 5)
{
    return findReferences(text, resolve, "phosphor-works", bare, limit);
}

} // namespace

TEST(References, RepositoryAndAlias)
{
    EXPECT_EQ(find("see Afterglow#87 and pz#12"), (std::vector<Reference>{{"Afterglow", 87}, {"PlasmaZones", 12}}));
    EXPECT_EQ(find("AFTERGLOW#1"), (std::vector<Reference>{{"Afterglow", 1}}));
}

TEST(References, OwnerAndRepository)
{
    EXPECT_EQ(find("phosphor-works/phosphor-libs#8"), (std::vector<Reference>{{"phosphor-libs", 8}}));
    EXPECT_TRUE(find("someone-else/phosphor-libs#8").empty());
}

TEST(References, ABareNumberNeedsARepository)
{
    EXPECT_TRUE(find("fixed in #12").empty());
    EXPECT_EQ(find("fixed in #12", "Afterglow"), (std::vector<Reference>{{"Afterglow", 12}}));
    EXPECT_EQ(find("PR #12.", "Afterglow"), (std::vector<Reference>{{"Afterglow", 12}}));
}

TEST(References, OtherHashesAreTalk)
{
    EXPECT_TRUE(find("C#12 is a language", "Afterglow").empty());
    EXPECT_TRUE(find("unknown#5").empty());
    EXPECT_TRUE(find("#12abc #0 #", "Afterglow").empty());
    EXPECT_TRUE(find("#12345678", "Afterglow").empty()) << "eight digits";
    EXPECT_TRUE(find("a/b/Afterglow#3").empty());
}

TEST(References, CodeAndLinksArePassedOver)
{
    EXPECT_TRUE(find("`Afterglow#1` and ```\nAfterglow#2\n```").empty());
    EXPECT_TRUE(find("https://github.com/phosphor-works/Afterglow/issues#3").empty());
    EXPECT_EQ(find("`x` Afterglow#4"), (std::vector<Reference>{{"Afterglow", 4}}));
}

TEST(References, EachOnceAndAtMostTheLimit)
{
    EXPECT_EQ(find("Afterglow#1 afterglow#1").size(), 1u);
    EXPECT_EQ(find("Afterglow#1 Afterglow#2 Afterglow#3", std::nullopt, 2).size(), 2u);
}

TEST(References, Punctuation)
{
    EXPECT_EQ(find("(Afterglow#5), Afterglow#6!"), (std::vector<Reference>{{"Afterglow", 5}, {"Afterglow", 6}}));
    EXPECT_EQ(find("**Afterglow#7**"), (std::vector<Reference>{{"Afterglow", 7}}));
}
