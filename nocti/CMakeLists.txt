# SPDX-FileCopyrightText: 2026 Phosphor Works Team
# SPDX-License-Identifier: GPL-3.0-or-later

set(NOCTI_VERSION ${PROJECT_VERSION})
configure_file(src/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

add_library(nocti_core STATIC
    src/Config.cpp
    src/Nocti.cpp
    src/People.cpp
    src/Preharness.cpp
)
target_include_directories(nocti_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(nocti_core PUBLIC bots_common)

add_executable(nocti src/main.cpp)
target_link_libraries(nocti PRIVATE nocti_core)
install(TARGETS nocti RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_executable(nocti_tests
        tests/ConfigTest.cpp
        tests/NoctiTest.cpp
        tests/PeopleTest.cpp
        tests/PreharnessTest.cpp
    )
    target_link_libraries(nocti_tests PRIVATE nocti_core GTest::gtest_main)
    gtest_discover_tests(nocti_tests)
endif()
