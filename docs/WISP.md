<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Wisp

Wisp is the Discord bot of phosphor-works. Its avatar is the Wisp mascot of
[branding](https://github.com/phosphor-works/branding) (`svg/mascot/avatar/wisp.svg`), and its messages use the
Phosphor palette.

## Replies

A message that names an issue or a pull request gets one reply, with a card for each:

| Written | Means |
| --- | --- |
| `Afterglow#87` | issue or pull request 87 of Afterglow (any case) |
| `pz#12` | an alias of `WISP_ALIASES` |
| `phosphor-works/phosphor-libs#8` | the organization, then the repository |
| `#87` | the repository of the channel (`WISP_CHANNEL_REPOS`), else `WISP_DEFAULT_REPO`, else nothing |

Wisp ignores numbers in code (`` `...` `` and fenced blocks) and in links. A reply links at most
`WISP_MAX_REFERENCES` items. A reply pings no one. Wisp edits a reply of the last `WISP_REPLY_DAYS` when its items
change, and looks every 10 minutes. When someone deletes the message, Wisp deletes its reply.

Wisp reads the text of a message only to find these names. It keeps no message text and no Discord author.

## Cards

Wisp keeps one message for each open pull request (`WISP_CARDS_PRS`) and each open issue (`WISP_CARDS_ISSUES`) of
the followed repositories. Each round (`WISP_POLL_SECONDS`) draws the cards again from GitHub:

- When the drawing changes, Wisp edits the card.
- Activity moves the card to the end of its channel: a push, a comment of a person, a change of state, or a CI run
  that ends. Wisp posts the card again, then deletes the old one.
- When an item closes, its card stays `WISP_CLOSED_HOURS` as it closed, then Wisp deletes it.
- When GitHub does not answer, the card stays as it was until the next round.

A pull request's card shows its branches, commits and size, its labels, the result of each workflow on its head
commit, and the latest comment of a person.

Each kind of message has its colour of the Phosphor palette:

| Colour | Kind |
| --- | --- |
| Blue `#3B82F6` | a pull request |
| Cyan `#22D3EE` | an issue |
| Purple `#A855F7` | a push (the feed) |
| Rose `#F43F5E` | a release (the feed) |
| Slate `#94A3B8` | a draft, or an item closed or merged |

The text says an item's state, and an icon says each CI run's result: ✅ passed, ❌ failed, ⏳ running.

Each card shows the organization's avatar and the repository above its title, and Wisp's avatar in its footer. Wisp
uses its own Discord avatar there, unless `WISP_ICON_URL` gives another.

## Feed

`WISP_FEED` gets a message for each push to the default branch of a followed repository, with its commits and the
result of its CI. Wisp edits that message until the runs end, for at most a day. The feed also gets a message for
each release, which Wisp edits for a day as files are added. On its first round, Wisp posts nothing for the
heads and releases that exist already.

## Settings

Wisp reads its settings from the environment. The service exports them from `/usr/local/etc/wisp.env`. The service reads that file as shell, so separate the items of a list with commas, with no spaces: in
`A=x y`, the shell runs `y` as a command. A `WISP_`
key that Wisp does not know stops it, and the log names the key.

| Key | Default | Value |
| --- | --- | --- |
| `WISP_TOKEN` | none (needed) | the Discord bot's token |
| `GITHUB_TOKEN` | none | a fine-grained token with read-only access to public repositories. The cards and the feed need it |
| `WISP_ORG` | `phosphor-works` | the GitHub organization |
| `WISP_REPOS` | each public, unarchived repository that is not a fork | the repositories it follows, separated by commas |
| `WISP_ALIASES` | none | `alias=Repository` pairs |
| `WISP_DEFAULT_REPO` | none | the repository of a bare `#123` |
| `WISP_CHANNEL_REPOS` | none | `channel=Repository` pairs: the repository of a bare `#123` in that channel |
| `WISP_CHANNELS` | every channel | the channel ids that it replies in |
| `WISP_CARDS_PRS` | none (off) | the channel id of the pull request cards |
| `WISP_CARDS_ISSUES` | none (off) | the channel id of the issue cards |
| `WISP_FEED` | none (off) | the channel id of the feed |
| `WISP_STATE` | none | the file of its state. The service sets it |
| `WISP_POLL_SECONDS` | 30 | the time between rounds, 10 to 3600 |
| `WISP_CLOSED_HOURS` | 24 | how long the card of a closed item stays |
| `WISP_REPLY_DAYS` | 7 | how long Wisp keeps a reply up to date |
| `WISP_MAX_REFERENCES` | 5 | the items in one reply, 1 to 10 |
| `WISP_ICON_URL` | the bot's Discord avatar | an HTTPS address of Wisp's avatar for the footers |

To get a channel id, turn on Developer Mode in Discord. Then right-click the channel and select Copy Channel ID.

## Install

### The Discord application

In the Discord Developer Portal:

1. Make an application named Wisp.
2. Set its avatar to the Wisp mascot. Discord takes a PNG, so render `svg/mascot/avatar/wisp.svg` of branding
   (librsvg):

       rsvg-convert --width 1024 --height 1024 --keep-aspect-ratio --output wisp-avatar.png svg/mascot/avatar/wisp.svg

3. Under Bot, reset the token and put it in the env file.
4. Under Bot, turn on **Message Content Intent**. Without it, Discord stops the bot with close code 4014.
5. Turn off **Public Bot**.
6. Invite it with only the permissions that it needs: View Channels, Send Messages, Send Messages in Threads, Embed
   Links and Read Message History:

       https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=274877991936

### The service

1. Install the package in the jail.
2. Fill in `/usr/local/etc/wisp.env` (root's, mode 600).
3. Turn the service on and start it:

       sysrc wisp_enable=YES
       service wisp start

The service runs `/usr/local/libexec/wisp` as the user `wisp` under daemon(8). When the program stops, daemon(8)
starts it again after 30 s. The log goes to syslog with the tag `wisp`. The state is
`/var/db/wisp/state.json` (`wisp_state` in rc.conf moves it).

| Variable | Default | Value |
| --- | --- | --- |
| `wisp_enable` | `NO` | `YES` starts it at boot |
| `wisp_env_file` | `/usr/local/etc/wisp.env` | its settings |
| `wisp_state` | `/var/db/wisp/state.json` | its state, exported as `WISP_STATE` |

## Security

- The bot listens on no port.
- Its process cannot be traced, also not by its own user: `PROC_TRACE_CTL` on FreeBSD, not dumpable on Linux. Its
  memory and environment hold the tokens.
- It reaches only `discord.com`, its gateway and `api.github.com`, through HTTPS. The jail can reach nothing more.
- Its state holds message ids, hashes, and the latest comment of each card (author, link, first words), as GitHub
  shows it publicly.

## When it stops

- When Discord refuses the token (401) or the intents (4013, 4014), Wisp stops with status 1. The last log line says
  why.
- A gateway connection that does not become ready within three heartbeat intervals is closed. So is one whose
  heartbeat Discord does not acknowledge. Wisp then connects again, and resumes the session when it can.
