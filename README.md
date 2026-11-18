<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-bots

The bots of [phosphor-works](https://github.com/phosphor-works), as FreeBSD packages for jails.

| Program | What it does |
| --- | --- |
| [Wisp](docs/WISP.md) (`wisp/`) | The Discord bot. It links the issues and pull requests that a message names, keeps a card for each open pull request and issue, and posts each push to a default branch and each release. |

`common/` is the library that the bots share: HTTP through libcurl, Discord's REST API and gateway, and GitHub's REST
API with ETags. It is C++20 and uses no Qt.

## Build and test

Install a C++20 compiler, CMake, Ninja, libcurl 7.86 or later (with WebSocket support) and GoogleTest. The build uses
the system's nlohmann/json. If the system has none, it downloads the archive that `cmake/BotsDependencies.cmake` pins.

    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build

## FreeBSD packages

On the FreeBSD release of the jails:

    pkg install cmake-core ninja pkgconf curl googletest nlohmann-json
    sh dist/freebsd/build.sh             # packages/wisp-<VERSION>.pkg

Then follow [Wisp](docs/WISP.md#install).

## License

GPL-3.0-or-later. nlohmann/json is MIT.
