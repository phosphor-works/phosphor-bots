<!-- SPDX-FileCopyrightText: 2026 Phosphor Works Team -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Glim

This page tells staff and operators how Glim works, how to set it up and how to install it. Glim files posts of the
Discord help forum as GitHub issues. It does nothing until someone mentions it. Its avatar is the Glim mascot of
[branding](https://github.com/phosphor-works/branding) (`svg/mascot/avatar/glim.svg`).

## Filing a post

Write one of these messages in a post of a forum that `GLIM_FORUMS` names:

| Written | Result |
| --- | --- |
| `@Glim` | Glim files the post in the repository of its project tag (`GLIM_TAG_REPOS`). |
| `@Glim afterglow` | Glim files the post in that repository. The word is a name, an alias of `GLIM_ALIASES`, or a tag. |
| `@Glim` again | Glim adds the messages since the last time to the issue, as a comment. |
| `@Glim help` | Glim tells how to use it. |

Only the author of the post and the members of `GLIM_STAFF_ROLES` can file a post. A reply to a message of Glim is
not a request. When Glim files or updates the issue, it tells the post, with a link.

If a post has no project tag, or more than one, Glim files it only when the mention names the repository.

## What goes to GitHub

A GitHub issue is public. Glim writes these items:

- The title of the post becomes the title of the issue.
- Each message of a person goes in, oldest first, under a heading with its author and time. Glim leaves out the
  messages of bots and the mentions of Glim with no other text.
- Each author shows as **Reporter** (the person who opened the post) or **Member N**. One person keeps the same
  number in the issue and its comments. A mention of a person becomes their name in the issue.
- Each `@word` gets a zero-width space after the `@`, so that it pings no one on GitHub.
- Glim keeps the attached files on our server (`GLIM_FILES_DIR`, served at `GLIM_FILES_URL`). The API of GitHub takes
  no files, and the links of Discord to files end after a day.
- A link to the Discord post goes in. Only members of the server can open it.

Glim handles each kind of attached file as this table shows:

| Kind | Result in the issue |
| --- | --- |
| Pictures (PNG, JPEG, WebP, GIF) | Shown |
| Videos (MP4, MOV, WebM) | Linked |
| Text files (a log, a configuration) | Put in the issue, cut to `GLIM_TEXT_LIMIT` bytes, and linked whole |
| Archives (zip, gz, xz, zst, 7z) | Linked as downloads |
| Other kinds | Only named |

Glim reads the kind of a file from its bytes, never from its name. Before Glim keeps a file, it removes the metadata:

- The EXIF of a photo (its place, its camera, its time), but not its orientation
- XMP and IPTC
- The text and time of a PNG
- The comments of a GIF
- The user data and location of an MP4 or a MOV
- The tags of a WebM

In a text file, Glim changes the home folders to `~`. The address of a file holds the id of the post and a number,
not the name of the file.

Glim cannot see names that people write in their own words. Staff check the issue before they ask for an update.

## When the issue closes

Each `GLIM_POLL_SECONDS`, Glim looks at the issues of the posts that it filed:

- When an issue closes, Glim tells the post.
- When the issue is completed, Glim also adds the forum tag `GLIM_SOLVED_TAG` to the post.
- When an issue opens again, Glim tells the post.

## The issues page

Glim writes the data of the issues page into `GLIM_FILES_DIR/api/`:

| File | Contents |
| --- | --- |
| `posts.json` | Each filed post, with its repository, issue, state, dates and number of kept files |
| `p/<post>.json` | One post with its files |

The data holds the title of the issue and the id of the post, as the addresses of the files do. It holds no Discord
name or id of a person. The website (phosphor-works/website) builds the page itself, and the page is served next to
the files.

With `GLIM_STAFF_LISTEN`, Glim also answers its staff on `/staff/api/`:

| Request | Result |
| --- | --- |
| `GET /staff/api/me` | Tells who is signed in |
| `GET /staff/api/posts` | Gives each filed post, with its files and the files that staff took down |
| `POST /staff/api/posts/<post>/update` | Adds the new messages of the post to its issue, and tells the post |
| `POST /staff/api/posts/<post>/files/<n>/remove` | Takes down file `<n>` (from 1) of the post |
| `POST /staff/api/posts/<post>/unlink` | Stops following the post. `?files=remove` also takes its files down. |

Cloudflare Access is in front of `/staff`. Access puts a token on each request (`Cf-Access-Jwt-Assertion`). Glim checks
that the keys of the team signed the token, that the token is for the application `GLIM_ACCESS_AUD`, and that the token
is not expired. Thus Glim refuses a request that reaches the API through another path. A `POST` also needs the header
`X-Glim-Staff`, which a form of another site cannot send. Glim logs each action with the email of the staff member.

## Settings

Glim reads its settings from the environment. The service exports them from `/usr/local/etc/glim.env`. The service
reads that file as shell, so separate the items of a list with commas and no spaces. For example, in `A=x y`, the shell
runs `y` as a command.

If Glim finds a `GLIM_` key that it does not know, it stops, and the log names the key. If a number is outside its
range, Glim also stops, and the log names the key.

| Key | Default | Value |
| --- | --- | --- |
| `GLIM_TOKEN` | none (needed) | The token of the Discord bot |
| `GLIM_APP_ID` | none (needed) | The ID of the GitHub App |
| `GLIM_APP_KEY` | none (needed) | The absolute path of the private key of the GitHub App |
| `GLIM_FORUMS` | none (needed) | The channel ids of the forums whose posts Glim files |
| `GLIM_ORG` | `phosphor-works` | The GitHub organization |
| `GLIM_TAG_REPOS` | none | `tag=Repository` pairs. Each pair gives the repository of a tag of a post. |
| `GLIM_ALIASES` | none | `alias=Repository` pairs for a mention |
| `GLIM_STAFF_ROLES` | none | The role ids that can file each post |
| `GLIM_SOLVED_TAG` | `solved` | The forum tag for a completed issue. If empty, Glim adds no tag. |
| `GLIM_LABELS` | none | The labels of a new issue. Each label must exist in the repository. |
| `GLIM_STATE` | none | The file of its state. The service sets it. |
| `GLIM_POLL_SECONDS` | 300 | The time between looks at the issues, from 60 to 3600 seconds |
| `GLIM_TEXT_LIMIT` | 32768 | The bytes of each attached text file in the issue, from 1024 to 60000 |
| `GLIM_FILES_DIR` | none | The folder where Glim keeps the attachments. Set it with `GLIM_FILES_URL`. |
| `GLIM_FILES_URL` | none | The HTTPS address that serves `GLIM_FILES_DIR` |
| `GLIM_FILE_MIB` | 25 | The maximum size of one kept file, from 1 to 500 MiB |
| `GLIM_POST_MIB` | 100 | The maximum size of the kept files of one filing, from 1 to 2000 MiB |
| `GLIM_STAFF_LISTEN` | none (no staff API) | The address and port of the staff API in a jail, for example `10.17.0.7:8090` |
| `GLIM_ACCESS_TEAM` | none | The Cloudflare Zero Trust team in front of the staff API |
| `GLIM_ACCESS_AUD` | none | The AUD tag of its Access application |
| `GLIM_STAFF_EMAILS` | anyone Access lets in | The emails that can do actions, separated by commas |

## Install

### The GitHub App

In the settings of the organization, go to Developer settings, then GitHub Apps. Select New GitHub App:

1. Set the name to Phosphor Glim. App names are unique across GitHub, and "Glim" is taken. The issues of the app show
   as `phosphor-glim[bot]`.
2. Set the Homepage URL to `https://github.com/phosphor-works/phosphor-bots`.
3. For the logo, render `svg/mascot/avatar/glim.svg` of branding to a PNG.
4. Under Webhook, clear **Active**. Glim asks GitHub, and GitHub never calls Glim.
5. Under Repository permissions, set **Issues: Read and write**. Metadata becomes read-only automatically. Give no
   other permission.
6. Under "Where can this GitHub App be installed", select **Only on this account**.
7. Create the app.
8. Write down its **App ID**.
9. In the left sidebar, select **Credentials**, then **Key pairs**, then **New key**. GitHub downloads a `.pem` file
   and keeps only its public half.
10. Install the app on the organization, on all repositories.

### The Discord application

In the Discord Developer Portal:

1. Make an application named Glim, with the Glim mascot as its avatar (a PNG).
2. Under Bot, reset the token.
3. Put the token in the env file.
4. Turn on **Message Content Intent**. Glim reads the messages of the post.
5. Set the Install Link to None.
6. Turn off **Public Bot**.
7. Invite the bot with only the permissions that it needs. These are View Channels, Send Messages, Send Messages in
   Threads, Embed Links, Read Message History, and Manage Threads (to add the solved tag to the post of a person):

   ```
   https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=292057861120
   ```

### The file server

Serve `GLIM_FILES_DIR` read-only from another jail. Then the jail that answers the internet holds no token and cannot
change the files. The server must obey these rules:

- Serve only the names that Glim writes (`/<post>/<message>-<n>.<extension>`).
- Send `X-Content-Type-Options: nosniff`.
- Send a Content-Security-Policy of `default-src 'none'; img-src 'self'; media-src 'self'; sandbox`.
- Send archives with `Content-Disposition: attachment`.

To take a file down, delete it from the folder.

### The service

1. Install the package in the jail.
2. Put the key of the app in `/usr/local/etc/glim/app.pem`, with owner `glim` and mode 400.
3. Fill in `/usr/local/etc/glim.env` (owner root, mode 600).
4. Turn the service on and start it:

   ```sh
   sysrc glim_enable=YES
   service glim start
   ```

The service runs `/usr/local/libexec/glim` as the user `glim` under daemon(8). Its log goes to syslog with the tag
`glim`. Its state is in `/var/db/glim/state.json`. For each filed post, the state holds the issue, the last filed
message and the Discord ids of the authors. These ids leave the server only as "Member N".

## Security

- The bot listens on no port. Debuggers of its own user cannot read its memory.
- It writes only issues and comments to GitHub, through an app that has no other permission. Its token lasts one hour.
- The key of the app never leaves the jail. Glim reads it one time, when it starts.

## Limits

| Item | Limit | At the limit |
| --- | --- | --- |
| One kept file | `GLIM_FILE_MIB` MiB | Glim does not keep the file. |
| All kept files of one filing | `GLIM_POST_MIB` MiB | Glim does not keep more files. |
| One issue or one comment | 60000 characters | Glim counts the messages after the limit and adds a link to the post. |
