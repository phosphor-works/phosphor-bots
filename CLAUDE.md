<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-bots

The bots of phosphor-works, in C++20 with no Qt. Wisp is the project's Discord bot, and Glim files help
forum posts as GitHub issues. Both run as FreeBSD packages in jails, and both build and test on Linux too.

The rules every phosphor-works repository follows come from the workspace repository, imported below. This file adds
phosphor-bots's own. The shared Claude Code tooling (the command guard, `phosphor-works:code-audit`, the shared
reviewers, the lesson index) comes from the `phosphor-works` plugin, which `.claude/settings.json` enables and
`workspace/tools/setup` installs.

@../workspace/claude/conventions.md

## Layout

| Folder | What it is |
| --- | --- |
| `common/` | `bots_common`, namespace `Bots`: HTTP through libcurl, Discord's REST API and gateway, GitHub's REST API with ETags and as a GitHub App, Cloudflare Access JWTs, a small HTTP server, logging, the brand colors (`Brand.h`) |
| `wisp/` | `wisp`, namespace `Wisp`: links the issues and pull requests a message names, keeps a card for each open pull request and issue, and posts pushes to a default branch and releases |
| `glim/` | `glim`: files a help forum post as a GitHub issue when someone mentions it, adds what is new on the next mention, says in the post when the issue closes, and serves the staff API |
| `dist/freebsd/` | `build.sh`, and for each bot the package description, plist, rc script, post-install script and env sample |
| `docs/` | `WISP.md` and `GLIM.md`: behavior, settings tables, install and security |

Each bot is a static `<bot>_core` library, the executable (installed to `libexec/`) and a `<bot>_tests` GoogleTest
binary. A new bot is one more folder of this shape, added in the root `CMakeLists.txt`.

## Build and test

Run the build inside the memory cap of the conventions. The shared presets build with 3 jobs.

```sh
cmake --workflow --preset dev            # configure into build/, build, test
ctest --test-dir build -R <name>         # one test
```

- Needs a C++20 compiler, CMake 3.25, Ninja, libcurl 7.86 or later with WebSocket support, OpenSSL 3 and GoogleTest.
- nlohmann/json comes from the system. If the system has none, `cmake/BotsDependencies.cmake` downloads the archive
  it pins (version, URL and SHA-256, changed together).
- The root `CMakeLists.txt` sets `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. A change builds with no warnings
  under GCC and under Clang.
- FreeBSD 15 builds with clang 19 and libc++ 19, which has no float `std::from_chars`. Use `std::strtod` for a
  floating-point parse (the program sets no locale). Integer `std::from_chars` is fine.
- `sh dist/freebsd/build.sh [OUT]` makes `wisp-<VERSION>.pkg` and `glim-<VERSION>.pkg` and runs the tests first. It
  runs only on FreeBSD. The version comes from `project()` in the root `CMakeLists.txt`.

CI is the shared C++ CI of the workspace repository (`cpp-ci.yml`): the `dev` preset on Arch with GCC, with a warning
failing the build, the tests, a staged install and a clang-format lint over every tracked file. There is no flake, so
the Nix jobs are off. CI does not build with Clang or on FreeBSD: check those locally.

## Rules

- Land work through a pull request. The user merges.
- A setting is an environment key with a default, read in the bot's `Config.cpp`. A `WISP_` or `GLIM_` key that the
  bot does not know stops it, and the log names the key. A new key goes in `KnownKeys`, the settings table of its
  `docs/` page and the `dist/freebsd/<bot>.env.sample`. The service reads the env file as shell, so a list
  separates its items with commas and no spaces.
- No token on a command line or in a log line.
- A message's look follows the Phosphor brand. The colors are in `common/src/Brand.h`, from `palette/phosphor.toml`
  of phosphor-works/branding.
- Format C++ with `clang-format -i` and the repository's `.clang-format`.
- `docs/*.md` and the package descriptions are user-facing text.

## Other repositories

- website: the issues pages on issues.phosphor-works.org are built by the website (`src/pages/issues/`). Glim
  writes the data they show into `GLIM_FILES_DIR/api/` (`posts.json` and `p/<post>.json`). A change to that JSON is a
  change set with the website's `src/scripts/issues.ts`.
- branding: the palette in `Brand.h` is a copy. Change it when the branding palette changes.

## License

REUSE-style: the license texts are in `LICENSES/` (`GPL-3.0-or-later`, `CC0-1.0`). Code and documents are
`GPL-3.0-or-later`. `.gitignore` is `CC0-1.0`. `REUSE.toml` licenses `.claude/settings.json`. nlohmann/json is MIT.
