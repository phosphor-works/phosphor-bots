<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-bots

phosphor-bots holds the bots of [phosphor-works](https://github.com/phosphor-works). They are FreeBSD packages for
jails.

## Features

| Program | What it does |
| --- | --- |
| [Wisp](docs/WISP.md) (`wisp/`) | The Discord bot. It links the issues and pull requests that a message names. It keeps a card for each open pull request and issue. It posts each push to a default branch and each release. |
| [Glim](docs/GLIM.md) (`glim/`) | It files a post of the help forum as a GitHub issue when someone mentions it. It adds the new messages on the next mention. It tells the post when the issue closes. |

`common/` is the library that the bots share. It gives HTTP through libcurl, the REST API and gateway of Discord, and
the REST API of GitHub with ETags and as a GitHub App. It is C++20 and uses no Qt.

## Install

On the FreeBSD release of the jails, install the build tools and make the packages:

```sh
pkg install cmake-core ninja pkgconf curl googletest nlohmann-json
sh dist/freebsd/build.sh             # packages/wisp-<VERSION>.pkg, packages/glim-<VERSION>.pkg
```

Then do the steps in [Wisp](docs/WISP.md#install) and [Glim](docs/GLIM.md#install).

## Build

Install these packages:

- A C++20 compiler
- CMake 3.25
- Ninja
- libcurl 7.86 or later, with WebSocket support
- OpenSSL 3
- GoogleTest

The build uses the nlohmann/json of the system. If the system has none, the build downloads the archive that
`cmake/BotsDependencies.cmake` pins.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Documentation

The [documentation index](docs/README.md) lists the pages about each bot.

## Contributing

The suite conventions are in the [workspace](https://github.com/phosphor-works/workspace) repository, in
`claude/conventions.md`. `CLAUDE.md` gives the layout and the rules of this repository. `lefthook install` sets up
the Git hooks from `lefthook.yml`. The hooks format the staged files and check the Conventional Commits message.

## License

phosphor-bots is licensed under GPL-3.0-or-later. nlohmann/json is licensed under MIT. The license texts of this
repository are in `LICENSES/`.
