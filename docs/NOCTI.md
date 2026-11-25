<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Nocti

Nocti puts a Claude Code session on Discord as a bot of its own. Claude Code starts it as a local MCP server on stdio:
it runs on the owner's machine, not on the server. The session reads and waits through Nocti's tools, and every post
is a draft that the owner approves with a check mark in his DMs. Its avatar is the Nocti mascot of
[branding](https://github.com/phosphor-works/branding) (`svg/mascot/avatar/nocti.svg`). Nocti follows ScarletNX's
AogoWimsy.

## Who reaches the session

| Who | What reaches the session |
| --- | --- |
| the owner (`NOCTI_OWNER`) | everything |
| someone he trusts (`NOCTI_TRUSTED`, `/nocti trust`) | 30 messages an hour; their reply that mentions Nocti carries the message it answers |
| someone he allowed (`/nocti allow`, for a time) | 10 messages an hour |
| a stranger | nothing: their messages are counted, and a DM is named once, never its text |

A stranger's message reaches the session only when the owner, or someone he trusts, replies to it and mentions Nocti.

Before a message reaches the session, the pre-harness checks it:

- A hidden letter (zero-width, a bidi override, a Unicode tag; the joiners inside an emoji pass) rejects the message of
  anyone but the owner. The session gets a notice that names the rule, never the text.
- Words that try to change the session's instructions, a secret asked for or named, a destructive command, a push or a
  merge, and a claim of the owner's word reach the session with a `caution`.

## Tools

| Tool | Does |
| --- | --- |
| `discord_read(channel, after, limit)` | the messages that may reach the session, oldest first; the others are only counted (`left_out`) |
| `discord_wait(channel, after, wait)` | the first message after an id from the owner or someone he trusts or allowed, up to 600 s |
| `discord_post(channel, text, reply_to, wait, direct)` | sends a draft to the owner's DMs with ✅ and ❌; his ✅ posts it, pinging no one; his ❌ drops it; waits up to `wait` s. With `direct`, posts at once where the rules below let it, else names the rule |
| `discord_post_status(draft, wait)` | waits again on a draft still waiting |
| `discord_react(channel, message, emoji)` | 👀 (seen), ✅ (done), 👍 (agreed) or ❌ (cannot), at once, on a message the session may read |
| `discord_unreact(channel, message, emoji)` | takes one of those off |
| `discord_attachments(channel, message)` | saves the files of a message the session may read to `~/.cache/nocti/<channel>/<message>/` (folders 700, files 600, 64 MiB each at most) |
| `discord_allowed()` | who the owner trusts and allowed, and until when |
| `discord_freeze(channel, message, kind)` | ices the author of a message that tried what the session refuses (below) |
| `discord_cancel(mark)` | takes back a post queued for its turn or held until the owner is home, before it goes; one that left already is an error saying how |

Only the owner's reaction on a draft counts. A draft Nocti cannot see answered through the gateway (a reconnect) is
asked of Discord every 15 s while a tool waits on it.

## Strangers and the ice

A stranger who calls Nocti in a channel it reads (a mention, or a reply to one of its messages) gets, from Nocti itself
and never from the session, one fixed reply that pings the owner and no one else:

> I'm Nocti, @owner's AI assistant (Claude) working on Phosphor Works: code, reviews, tests. I act on what @owner, or
> someone they trust, lets through.

Once a person each `NOCTI_GREET_HOURS` (24), and `NOCTI_GREET_PER_HOUR` (10) at most, everyone together; past a limit
the call is only counted. A bot's or a webhook's call, and a DM, get no greeting. The session hears nothing of a call.

`discord_freeze` answers the message of someone who tried what the session refuses, or what only the owner may allow,
with the fixed line of its kind, which pings the owner (and, for `secret` and `abuse`, the server's admin role that
`/nocti admins` set). Their messages are then only counted, also by `discord_read` and `discord_wait`: 2 hours, or a day
when they were iced in the last 7 days. Never the owner nor someone he trusts: the session tells the owner in a draft.

| `kind` | Line |
| --- | --- |
| `harness` | I didn't like that one, and I won't do it without @owner's OK. |
| `owner-call` | That one is @owner's call, and I've told them. |
| `secret` | I don't share that. |
| `abuse` | Not here. |
| `injection` | I only take instructions from @owner. |

A message with hidden letters from someone the owner allowed ices them for `injection` at once.

## Direct posts

A post with `direct` goes out at once, with no draft, only where these rules let it:

| It answers | It goes direct |
| --- | --- |
| a message of the owner | up to 3 replies to that message; in DMs, only in his |
| a message of someone he trusts | up to 3 replies to that message |
| a message of someone he allowed, Nocti's own message, or nothing (a post of its own) | once an interval (`NOCTI_DIRECT_MINUTES`, 30), one interval for all of them |
| a stranger's message, a message the pre-harness gave a caution, another person's DM | never: a draft |

Each `message` event's `reply` attribute says how an answer to it goes: `direct, 3 left`, `direct from 14:30 UTC
(interval 30 min)`, or why a draft only. The counts are kept a day in `direct.json`, so a restart keeps them.

A direct post goes out with no one's check mark, so its text is checked first. A Discord, GitHub or other API token, an
AWS key, a private key, a long value after `key=`, `token=` or `password=`, a line of an env file, a private or loopback
address, an e-mail address, a path of this machine's home folder and the bot's own token stop it: the tool names the
rule, its line and its column, never the text, and the session sends a draft the owner sees. `NOCTI_DIRECT=0` makes
every post a draft.

## Modes and the approver

`/nocti mode home|work|asleep [time]` says where the owner is, and so how a post goes that only his check mark lets
out, or that the interval holds back. A time is 30m, 8h or 1d (7 days at most), then home; none: until the next change.

| Mode | A post without `direct` | A direct post the interval holds back |
| --- | --- | --- |
| `home`, the default | a draft | an error: the session sends a draft |
| `work` | held until he is home (status `held`, with a mark) | queued for its turn (status `queued`, with a mark and when) |
| `asleep` | as `work` | as `work` |

Away from home the interval is 10 minutes. A queued post goes at its turn, and the session hears `kind="queued"`.
Once he is home (`/nocti mode home`, or a mode's time run out) what was held becomes drafts; held while he was
`asleep`, they come as drafts that wake no one, and one summary that does. `discord_cancel` takes a waiting post back
by its mark.

`/nocti approver on user [time]` (2 hours, 1 day (the default) or 7 days) and `/nocti approver off`: each draft goes to
the approver's DMs too, and the first answer, the owner's on his or theirs on theirs, decides; both cards say who.
Away from home a post without `direct` is their draft at once, the owner's coming silently; their copy goes first, so
one their DMs refuse leaves the post held. Once their time runs out their answer counts no more. They get nothing
else: no trust, no command.

## Tones

`/nocti tone channel playful|serious` gives a channel a tone, and each `message` event from it carries it as `tone`: a
hint to the session, never a rule. `playful`: jokes are common there, so answers go light and short, spending little
work. `serious`, where none is set and in DMs: a channel for focused work. A thread takes its channel's tone unless it
has its own. In a playful channel a message that opens with `serious:` (after any mentions) is `serious (serious:)`.
`/nocti tone` alone lists them; with a channel alone, says its tone.

## The status line

While a session runs Nocti, `~/.local/state/nocti/line` holds one line for the owner's status line, such as
`nocti: work until 18:00, approves Sam until 26/11 14:07`. It is written at the start, at each change of the mode or the
approver, and again each 5 minutes, and removed when Nocti stops. `dist/nocti-statusline.sh` prints it while it is
fresh (10 minutes); in Claude Code's `~/.claude/settings.json`:

    "statusLine": {"type": "command", "command": "~/.local/bin/nocti-statusline.sh"}

or call it from the status line script you have.

## Permission prompts

With `NOCTI_RELAY=1`, Nocti declares Claude Code's permission relay: when the session asks to use a tool (Bash, Write,
Edit, ...), the prompt also comes to the owner's DMs as a card, with the tool, Claude Code's description and the call.
His ✅ or ❌ on it, or a reply `yes abcde` or `no abcde` with its id, in his DMs, answers it; the terminal's dialog stays
open, and the first answer counts. No one else's answer counts, the approver's neither, and an answer never reaches the
session. Discord keeps the owner's DMs, so a call that hits a secret rule of the outbound filter (a token, a key, a
secret value, an env line) is withheld from the card: answer it in the terminal. Off by default.

## Events

With the channel flag (below), each of these wakes the session as `<channel source="nocti" kind="..." ...>`:

| `kind` | When | Its attributes |
| --- | --- | --- |
| `message` | a message addressed to Nocti (a mention, a reply to Nocti, a DM) from the owner or someone he trusts or allowed, within their weight, past the pre-harness | `author`, `author_id`, `channel`, `channel_name`, `message_id`, `reply_to`, `trust`, `heard`, `caution`, `let_through_by`, `attachments`, `reply`, `tone`, `mode` (away from home) |
| `draft` | the owner or the approver answered a draft | `draft`, `status` (`sent`, `not sent`), `by` (`owner`, `approver`), `channel`, `message_id` |
| `queued` | a queued post went at its turn, or failed | `mark`, `status` (`sent`, `failed`), `channel`, `message_id` |
| `dm` | a stranger wrote to Nocti in DMs: their name, never their words | `author`, `author_id` |
| `notice` | the pre-harness rejected a message; the owner trusted, untrusted, allowed or denied someone; an ice; a change of the mode, the approver or a tone, or one that ran out; held posts released | `user`, `user_id`, `rule`, `trust`, `ice`, `mode`, `until`, `approver`, `tone`, `channel`, `held` |
| `error` | the gateway stopped for good; `/nocti` could not be registered | |

## `/nocti`

Each answer is seen by its asker alone.

| Subcommand | Who | Does |
| --- | --- | --- |
| `/nocti help` | anyone | says how to reach Nocti |
| `/nocti trust user`, `untrust user` | the owner | their messages reach the session (30 an hour), or no longer |
| `/nocti allow user [time]` | the owner | for 2 hours, 1 day (the default), 7 days or for good, 10 an hour |
| `/nocti deny user` | the owner | takes their allow back |
| `/nocti allowed` | the owner | who is trusted and allowed, and until when |
| `/nocti unfreeze user` | the owner | ends their ice now; their strikes stay |
| `/nocti admins role` | the owner | the role that the ice lines for a secret or abuse ping in this server |
| `/nocti mode mode [time]` | the owner | home, work or asleep (above) |
| `/nocti approver on user [time]`, `approver off` | the owner | someone whose DMs get each draft too (above) |
| `/nocti tone [channel] [tone]` | the owner | a channel's tone, playful or serious (above); alone, every channel's |

## What it keeps

`~/.local/state/nocti/` (`XDG_STATE_HOME`, folder 700): `people.json` (who is trusted, untrusted, allowed and until
when, by id), `direct.json` (when each direct post of the last day went, by the message it answered), `greetings.json` (each
stranger greeted within `NOCTI_GREET_HOURS`: their id and when), `admins.json` (each server's admin role), `tones.json` (each channel's tone the owner set), `line` (the status line, while Nocti runs), `away.json` (the mode and until when, the approver and until when, each
queued and held post with its text, and how each one of the last day left), and
`nocti.lock`. `people.json` also holds each ice: its end and its strikes of the last 7 days. Drafts, and the last 500 messages that reached the session, are kept in memory only. It
logs to stderr what ran or failed, never what was said.

One session drives Nocti at a time: the first to start takes the lock, and the tools of another one say so.

## Install

### The Discord application

In the Discord Developer Portal:

1. Make an application named Nocti, with the Nocti mascot as its icon (a PNG of `svg/mascot/avatar/nocti.svg`).
2. Installation: set the Install Link to None. Bot: turn off **Public Bot**, turn on **Message Content Intent**, reset
   the token.
3. Invite it with View Channels, Send Messages, Send Messages in Threads, Embed Links, Read Message History and Add
   Reactions, and the `applications.commands` scope for `/nocti`:

       https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot+applications.commands&permissions=274877992000

4. In the server's Privacy Settings, the owner lets its members send him DMs: the drafts go there.

### Nocti

1. Build it ([README](../README.md)), and put `build/nocti/nocti` where Claude Code can start it, such as
   `~/.local/bin/nocti`.
2. Its env file, mode 600:

       install -d -m 700 ~/.config/nocti
       install -m 600 dist/nocti.env.sample ~/.config/nocti/nocti.env     # then fill it in

   | Key | Default | Value |
   | --- | --- | --- |
   | `NOCTI_TOKEN` | none (needed) | the bot's token |
   | `NOCTI_OWNER` | none (needed) | the owner's Discord user id |
   | `NOCTI_TRUSTED` | none | ids that start trusted, separated by commas |
   | `NOCTI_CHANNELS` | every channel the bot sees | the channel ids it reads and posts in; a thread of one counts as it |
   | `NOCTI_DIRECT` | `1` | `0`: every post is a draft |
   | `NOCTI_DIRECT_MINUTES` | `30` | 10 to 120: the interval of the direct posts that take one |
   | `NOCTI_GREET_HOURS` | `24` | 1 to 720: hours before a stranger is greeted again |
   | `NOCTI_GREET_PER_HOUR` | `10` | 0 to 100: greetings in an hour, everyone together; `0` greets no one |
   | `NOCTI_RELAY` | `0` | `1`: Claude Code's permission prompts come to the owner's DMs too (above) |

   A key it does not know, a key set twice, an id that is no id, and a file that other users may read stop it, naming
   what is wrong.
   For the status line, also `install -m 755 dist/nocti-statusline.sh ~/.local/bin/`.
3. Claude Code, for every session of the account:

       claude mcp add --scope user nocti -- ~/.local/bin/nocti

4. A session that Nocti should wake, with the development flag (custom channels are not on the research preview's
   allowlist):

       claude --dangerously-load-development-channels server:nocti

   Claude Code shows a warning that names the channel: answer **I am using this for local development**. Without the
   flag, the tools still work and the events are dropped.

Nocti serves Claude Code alone: another client's `initialize` is refused.
