# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set(WISP_VERSION ${PROJECT_VERSION})
configure_file(src/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

add_library(wisp_core STATIC
    src/Cards.cpp
    src/Config.cpp
    src/Embeds.cpp
    src/References.cpp
    src/State.cpp
    src/Wisp.cpp
)
target_include_directories(wisp_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(wisp_core PUBLIC bots_common)

add_executable(wisp src/main.cpp)
target_link_libraries(wisp PRIVATE wisp_core)
install(TARGETS wisp RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

if(BUILD_TESTING)
    add_executable(wisp_tests
        tests/CardsTest.cpp
        tests/ConfigTest.cpp
        tests/EmbedsTest.cpp
        tests/ReferencesTest.cpp
        tests/StateTest.cpp
    )
    target_link_libraries(wisp_tests PRIVATE wisp_core GTest::gtest_main)
    gtest_discover_tests(wisp_tests)
endif()
