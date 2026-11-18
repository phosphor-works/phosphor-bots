// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "State.h"

#include <gtest/gtest.h>

using namespace Glim;

TEST(GlimState, RoundTrip)
{
    State state;
    PostRecord record;
    record.repo = "PlasmaZones";
    record.number = 145;
    record.url = "https://github.com/phosphor-works/PlasmaZones/issues/145";
    record.lastMessage = 1572712569028874534ULL;
    record.participants.reporter = 205390473609609217ULL;
    record.participants.members = {{1526341393050763326ULL, 1}, {1572706815177793569ULL, 2}};
    record.issueState = "closed";
    state.posts[1572712575194497116ULL] = record;
    EXPECT_EQ(stateFromJson(toJson(state)), state);
    EXPECT_TRUE(toJson(state)["posts"]["1572712575194497116"]["last"].is_string());
}
