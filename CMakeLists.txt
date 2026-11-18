# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-bots: the bots and services of phosphor-works. common/ is the
# library they share (HTTP, Discord's REST API and gateway, GitHub's API);
# each bot is a folder of its own.

cmake_minimum_required(VERSION 3.25)
project(phosphor-bots VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(GNUInstallDirs)
include(cmake/BotsDependencies.cmake)

find_package(CURL 7.86 REQUIRED)
find_package(Threads REQUIRED)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

add_subdirectory(common)
add_subdirectory(wisp)
