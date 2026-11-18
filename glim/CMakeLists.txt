# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set(GLIM_VERSION ${PROJECT_VERSION})
configure_file(src/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

add_library(glim_core STATIC
    src/Config.cpp
    src/Glim.cpp
    src/Media.cpp
    src/Report.cpp
    src/State.cpp
)
target_include_directories(glim_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(glim_core PUBLIC bots_common)

add_executable(glim src/main.cpp)
target_link_libraries(glim PRIVATE glim_core)
install(TARGETS glim RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

if(BUILD_TESTING)
    add_executable(glim_tests
        tests/ConfigTest.cpp
        tests/GlimTest.cpp
        tests/MediaTest.cpp
        tests/ReportTest.cpp
        tests/StateTest.cpp
    )
    target_link_libraries(glim_tests PRIVATE glim_core GTest::gtest_main)
    gtest_discover_tests(glim_tests)
endif()
