# SPDX-FileCopyrightText: 2026 Phosphor Works Team
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(bots_common STATIC
    src/AccessJwt.cpp
    src/Discord.cpp
    src/Files.cpp
    src/Gateway.cpp
    src/GatewaySession.cpp
    src/GitHub.cpp
    src/GitHubApp.cpp
    src/Http.cpp
    src/HttpServer.cpp
    src/Log.cpp
    src/McpServer.cpp
    src/Service.cpp
)
target_include_directories(bots_common PUBLIC src)
target_link_libraries(bots_common PUBLIC CURL::libcurl OpenSSL::Crypto Threads::Threads nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_executable(bots_common_tests
        tests/AccessJwtTest.cpp
        tests/DiscordTest.cpp
        tests/GatewaySessionTest.cpp
        tests/GitHubAppTest.cpp
        tests/GitHubTest.cpp
        tests/HttpServerTest.cpp
        tests/McpServerTest.cpp
    )
    target_link_libraries(bots_common_tests PRIVATE bots_common GTest::gtest_main)
    gtest_discover_tests(bots_common_tests)
endif()
