<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# phosphor-bots

The bots of phosphor-works: `common/` (namespace `Bots`, shared by every bot) and one folder for each bot (`wisp/`,
namespace `Wisp`). C++20, no Qt. They run in FreeBSD jails (`dist/freebsd/`) and are tested on Linux too.

## Rules

- Land work through a pull request. The user merges.
- Zero warnings with GCC and Clang. FreeBSD 15 builds with clang 19 and libc++ 19: libc++ 19 has no float
  `std::from_chars`, so use `std::strtod` (the program sets no locale).
- Every file starts with `SPDX-FileCopyrightText: 2026 fuddlesworth` and an `SPDX-License-Identifier`.
- A setting is an environment key with a default. A `WISP_` key that the bot does not know stops it.
- No token on a command line or in a log line.
- A message's look follows the Phosphor brand (`phosphor-works/branding`, `palette/phosphor.toml`).
- Format C++ with `clang-format -i` (the `.clang-format` of phosphor-libs).
- Documents use controlled English: short sentences, one instruction in each step, tables for settings.
- Commit subject: an imperative sentence in sentence case, no prefix, no full stop.

## Build and test

| Task | Command |
| --- | --- |
| Build | `cmake -S . -B build -G Ninja && cmake --build build` |
| Tests | `ctest --test-dir build` |
| FreeBSD packages | `sh dist/freebsd/build.sh` (on FreeBSD) |
