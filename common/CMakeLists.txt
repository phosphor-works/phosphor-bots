# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(bots_common STATIC
    src/Discord.cpp
    src/Gateway.cpp
    src/GatewaySession.cpp
    src/GitHub.cpp
    src/Http.cpp
    src/Log.cpp
)
target_include_directories(bots_common PUBLIC src)
target_link_libraries(bots_common PUBLIC CURL::libcurl Threads::Threads nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_executable(bots_common_tests
        tests/DiscordTest.cpp
        tests/GatewaySessionTest.cpp
        tests/GitHubTest.cpp
    )
    target_link_libraries(bots_common_tests PRIVATE bots_common GTest::gtest_main)
    gtest_discover_tests(bots_common_tests)
endif()
