#!/bin/sh
# SPDX-FileCopyrightText: 2026 Phosphor Works Team
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prints Nocti's line ("nocti: work until 18:00, approves Sam until 26/11 14:07")
# for Claude Code's status line, while a session runs Nocti: a running Nocti
# writes it again each 5 minutes, so a line older than 10 is a stopped one's.
# Claude Code's JSON on stdin is not needed.
line="${XDG_STATE_HOME:-$HOME/.local/state}/nocti/line"
if [ -n "$(find "$line" -mmin -10 2>/dev/null)" ]; then
    cat "$line"
fi
exit 0
