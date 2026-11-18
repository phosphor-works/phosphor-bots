# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# nlohmann/json (MIT): the system's copy when it has one (FreeBSD's
# devel/nlohmann-json, Arch's nlohmann-json), else the release archive pinned
# here by URL and SHA-256. The hash matches FreeBSD's ports distinfo for
# nlohmann-json-v3.12.0_GH0.tar.gz.
#
# Bumping the pin: change the version, URL and SHA256 together, with the hash
# taken from a download that you checked against a second source.

include_guard(GLOBAL)

set(BOTS_DEP_nlohmann_json_VERSION "3.12.0")
set(BOTS_DEP_nlohmann_json_URL "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz")
set(BOTS_DEP_nlohmann_json_SHA256 "4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187")

find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        URL "${BOTS_DEP_nlohmann_json_URL}"
        URL_HASH "SHA256=${BOTS_DEP_nlohmann_json_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SYSTEM
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(STATUS "nlohmann/json ${BOTS_DEP_nlohmann_json_VERSION}: the pinned archive")
else()
    message(STATUS "nlohmann/json ${nlohmann_json_VERSION}: the system's")
endif()
