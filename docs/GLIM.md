<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Glim

Glim files posts of the Discord help forum as GitHub issues. It does nothing until someone mentions it. Its avatar is
the Glim mascot of [branding](https://github.com/phosphor-works/branding) (`svg/mascot/avatar/glim.svg`).

## Filing a post

In a post of a forum that `GLIM_FORUMS` names:

| Written | Does |
| --- | --- |
| `@Glim` | files the post in the repository of its project tag (`GLIM_TAG_REPOS`) |
| `@Glim afterglow` | files the post in that repository (a name, an alias of `GLIM_ALIASES`, or a tag) |
| `@Glim` again | adds the messages since the last time to the issue, as a comment |
| `@Glim help` | says how to use it |

Only the author of the post and the members of `GLIM_STAFF_ROLES` can file a post. A reply to one of Glim's messages
is no request. Glim tells the post when the issue is filed or updated, with a link.

A post with no project tag, or with more than one, is not filed until the mention names the repository.

## What goes to GitHub

A GitHub issue is public. Glim writes:

- The post's title as the issue's title.
- Each message of a person, oldest first, under a heading with its author and time. Bots' messages and bare
  mentions of Glim are left out.
- Each author as **Reporter** (the person who opened the post) or **Member N**. One person keeps the same number in
  the issue and its comments. A mention of someone becomes their name in the issue.
- An `@word` with a zero-width space after the `@`, so that it pings no one on GitHub.
- Each attached text file (a log, a configuration) in the issue, cut to `GLIM_TEXT_LIMIT` bytes. Pictures and other
  files are named, with a note that they are in the Discord post. Discord's links to files end after a day, so the
  issue does not link them.
- A link to the Discord post, which only members of the server can open.

An issue or a comment holds at most 60000 characters. The messages past that are counted, with a link to the post.

Glim cannot see names that people write in their own words. Staff check the issue before they ask for an update.

## When the issue closes

Every `GLIM_POLL_SECONDS`, Glim looks at the issues of the posts it filed. When an issue closes, Glim says so in its
post. When the issue is completed, it also adds the forum tag `GLIM_SOLVED_TAG` to the post. When an issue opens
again, Glim says that too.

## Settings

Glim reads its settings from the environment. The service exports them from `/usr/local/etc/glim.env`. The service reads that file as shell, so separate the items of a list with commas, with no spaces: in
`A=x y`, the shell runs `y` as a command. A `GLIM_` key
that Glim does not know stops it, and the log names the key.

| Key | Default | Value |
| --- | --- | --- |
| `GLIM_TOKEN` | none (needed) | the Discord bot's token |
| `GLIM_APP_ID` | none (needed) | the GitHub App's ID |
| `GLIM_APP_KEY` | none (needed) | the absolute path of the GitHub App's private key |
| `GLIM_FORUMS` | none (needed) | the channel ids of the forums whose posts it files |
| `GLIM_ORG` | `phosphor-works` | the GitHub organization |
| `GLIM_TAG_REPOS` | none | `tag=Repository` pairs: the repository of a post's tag |
| `GLIM_ALIASES` | none | `alias=Repository` pairs for a mention |
| `GLIM_STAFF_ROLES` | none | role ids that may file any post |
| `GLIM_SOLVED_TAG` | `solved` | the forum tag for a completed issue; empty: none |
| `GLIM_LABELS` | none | labels of a new issue; each must exist in the repository |
| `GLIM_STATE` | none | the file of its state. The service sets it |
| `GLIM_POLL_SECONDS` | 300 | the time between looks at the issues, 60 to 3600 |
| `GLIM_TEXT_LIMIT` | 32768 | the bytes of each attached text file in the issue, 1024 to 60000 |

## Install

### The GitHub App

In the organization's settings, under Developer settings, GitHub Apps, select New GitHub App:

1. Name: Phosphor Glim. App names are unique across GitHub, and "Glim" is taken. Its issues show as
   `phosphor-glim[bot]`. Homepage URL: `https://github.com/phosphor-works/phosphor-bots`.
2. Logo: render `svg/mascot/avatar/glim.svg` of branding to a PNG.
3. Webhook: clear **Active**. Glim asks GitHub; GitHub never calls Glim.
4. Repository permissions: **Issues: Read and write**. Metadata becomes read-only by itself. Give no other
   permission.
5. Where can this GitHub App be installed: **Only on this account**.
6. Create the app. Note its **App ID**.
7. Generate a private key. GitHub downloads a `.pem` file.
8. Install the app on the organization, on all repositories.

### The Discord application

In the Discord Developer Portal:

1. Make an application named Glim, with the Glim mascot as its avatar (a PNG).
2. Under Bot, reset the token and put it in the env file. Turn on **Message Content Intent**: Glim reads the post's
   messages. Turn off **Public Bot** (set the Install Link to None first).
3. Invite it with only the permissions that it needs: View Channels, Send Messages, Send Messages in Threads, Embed
   Links, Read Message History, and Manage Threads (to add the solved tag to someone's post):

       https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=292057861120

### The service

1. Install the package in the jail.
2. Put the app's key in `/usr/local/etc/glim/app.pem`, owner `glim`, mode 400.
3. Fill in `/usr/local/etc/glim.env` (root's, mode 600).
4. Turn the service on and start it:

       sysrc glim_enable=YES
       service glim start

The service runs `/usr/local/libexec/glim` as the user `glim` under daemon(8), its log in syslog with the tag `glim`.
Its state is `/var/db/glim/state.json`: for each filed post, its issue, the last message filed and the Discord ids
of its authors, which leave the server only as "Member N".

## Security

- The bot listens on no port, and debuggers of its own user cannot read its memory.
- It writes to GitHub only issues and comments, through an app that may do nothing else. Its token lasts an hour.
- The app's key never leaves the jail. Glim reads it once, at its start.
