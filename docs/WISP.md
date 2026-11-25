<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Wisp

This page tells operators how Wisp works, how to set it up and how to install it. Wisp is the Discord bot of
phosphor-works. Its avatar is the Wisp mascot of [branding](https://github.com/phosphor-works/branding)
(`svg/mascot/avatar/wisp.svg`), and its messages use the Phosphor palette.

## Replies

When a message names an issue or a pull request, Wisp sends one reply, with a card for each item:

| Written | Meaning |
| --- | --- |
| `Afterglow#87` | Issue or pull request 87 of Afterglow (in uppercase or lowercase) |
| `pz#12` | An alias of `WISP_ALIASES` |
| `phosphor-works/phosphor-libs#8` | The organization, then the repository |
| `#87` | The repository of the channel (`WISP_CHANNEL_REPOS`). If there is none, `WISP_DEFAULT_REPO`. If there is none, nothing. |

Wisp ignores numbers in code (`` `...` `` and fenced blocks) and in links. A reply links a maximum of
`WISP_MAX_REFERENCES` items. A reply pings no one.

Wisp looks at its replies every 10 minutes. When the items of a reply from the last `WISP_REPLY_DAYS` change, Wisp
edits the reply. When someone deletes the message, Wisp deletes its reply.

Wisp reads the text of a message only to find these names. It keeps no message text and no Discord author.

## Cards

Wisp keeps one message for each open pull request (`WISP_CARDS_PRS`) and each open issue (`WISP_CARDS_ISSUES`) of
the followed repositories. In each round (`WISP_POLL_SECONDS`), Wisp makes the cards again from GitHub:

- When the contents of a card change, Wisp edits the card.
- Activity moves the card to the end of its channel. Activity is a push, a comment of a person, a change of state, or
  the end of a CI run. Wisp posts the card again, then deletes the old card.
- When an item closes, its card stays as it was at the close for `WISP_CLOSED_HOURS`. Then Wisp deletes it.
- When GitHub does not answer, the card stays as it was until the next round.

The card of a pull request shows these items:

- Its branches, commits and size
- Its labels
- The result of each workflow on its head commit
- The latest comment of a person

Each kind of message has its color of the Phosphor palette:

| Color | Kind |
| --- | --- |
| Blue `#3B82F6` | A pull request |
| Cyan `#22D3EE` | An issue |
| Purple `#A855F7` | A push (the feed) |
| Rose `#F43F5E` | A release (the feed) |
| Slate `#94A3B8` | A draft, or an item that is closed or merged |

The text gives the state of an item. An icon gives the result of each CI run:

| Icon | Result |
| --- | --- |
| ✅ | Passed |
| ❌ | Failed |
| ⏳ | Running |

Each card shows the avatar of the organization and the repository above its title. Its footer shows the avatar of
Wisp. Wisp uses its own Discord avatar there, unless `WISP_ICON_URL` gives another avatar.

## Feed

For each push to the default branch of a followed repository, `WISP_FEED` gets a message with the commits and the
result of the CI. Wisp edits that message until the runs end, for a maximum of one day. The feed also gets a message
for each release. Wisp edits that message for one day while files are added. In its first round, Wisp posts nothing
for the heads and the releases that exist already.

## Settings

Wisp reads its settings from the environment. The service exports them from `/usr/local/etc/wisp.env`. The service
reads that file as shell, so separate the items of a list with commas and no spaces. For example, in `A=x y`, the shell
runs `y` as a command.

If Wisp finds a `WISP_` key that it does not know, it stops, and the log names the key. If a number is outside its
range, Wisp also stops, and the log names the key.

| Key | Default | Value |
| --- | --- | --- |
| `WISP_TOKEN` | none (needed) | The token of the Discord bot |
| `GITHUB_TOKEN` | none | A fine-grained token with read-only access to public repositories. The cards and the feed need it. |
| `WISP_ORG` | `phosphor-works` | The GitHub organization |
| `WISP_REPOS` | each public, unarchived repository that is not a fork | The repositories that Wisp follows, separated by commas |
| `WISP_ALIASES` | none | `alias=Repository` pairs |
| `WISP_DEFAULT_REPO` | none | The repository of a bare `#123` |
| `WISP_CHANNEL_REPOS` | none | `channel=Repository` pairs. Each pair gives the repository of a bare `#123` in that channel. |
| `WISP_CHANNELS` | every channel | The channel ids where Wisp replies |
| `WISP_CARDS_PRS` | none (off) | The channel id of the pull request cards |
| `WISP_CARDS_ISSUES` | none (off) | The channel id of the issue cards |
| `WISP_FEED` | none (off) | The channel id of the feed |
| `WISP_STATE` | none | The file of its state. The service sets it. |
| `WISP_POLL_SECONDS` | 30 | The time between rounds, from 10 to 3600 seconds |
| `WISP_CLOSED_HOURS` | 24 | The time that the card of a closed item stays, from 0 to 720 hours |
| `WISP_REPLY_DAYS` | 7 | The time that Wisp keeps a reply up to date, from 0 to 30 days |
| `WISP_MAX_REFERENCES` | 5 | The items in one reply, from 1 to 10 |
| `WISP_ICON_URL` | the bot's Discord avatar | An HTTPS address of the avatar of Wisp for the footers |

To get a channel id, turn on Developer Mode in Discord. Then right-click the channel and select Copy Channel ID.

## Install

### The Discord application

In the Discord Developer Portal:

1. Make an application named Wisp.
2. Set its avatar to the Wisp mascot. Discord takes a PNG, so render `svg/mascot/avatar/wisp.svg` of branding with
   librsvg:

   ```sh
   rsvg-convert --width 1024 --height 1024 --keep-aspect-ratio --output wisp-avatar.png svg/mascot/avatar/wisp.svg
   ```

3. Under Bot, reset the token.
4. Put the token in the env file.
5. Under Bot, turn on **Message Content Intent**. Without it, Discord stops the bot with close code 4014.
6. Turn off **Public Bot**.
7. Invite the bot with only the permissions that it needs. These are View Channels, Send Messages, Send Messages in
   Threads, Embed Links and Read Message History:

   ```
   https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=274877991936
   ```

### The service

1. Install the package in the jail.
2. Fill in `/usr/local/etc/wisp.env` (owner root, mode 600).
3. Turn the service on and start it:

   ```sh
   sysrc wisp_enable=YES
   service wisp start
   ```

The service runs `/usr/local/libexec/wisp` as the user `wisp` under daemon(8). When the program stops, daemon(8)
starts it again after 30 s. The log goes to syslog with the tag `wisp`. The state is in `/var/db/wisp/state.json`.
`wisp_state` in rc.conf moves it.

| Variable | Default | Value |
| --- | --- | --- |
| `wisp_enable` | `NO` | `YES` starts the service at boot. |
| `wisp_env_file` | `/usr/local/etc/wisp.env` | The settings file |
| `wisp_state` | `/var/db/wisp/state.json` | The state file, exported as `WISP_STATE` |

## Security

- The bot listens on no port.
- No process can trace it, also not a process of its own user. It uses `PROC_TRACE_CTL` on FreeBSD, and it is not
  dumpable on Linux. Its memory and environment hold the tokens.
- It connects only to `discord.com`, its gateway and `api.github.com`, through HTTPS. The jail can connect to nothing
  more.
- Its state holds message ids, hashes, and the latest comment of each card (author, link, first words), as GitHub
  shows it publicly.

## Troubleshooting

- If Discord refuses the token (401) or the intents (4013, 4014), Wisp stops with status 1. The last log line gives
  the cause.
- If a gateway connection does not become ready in three heartbeat intervals, Wisp closes it. Wisp also closes a
  connection when Discord does not acknowledge its heartbeat. Then Wisp connects again, and resumes the session if it
  can.
