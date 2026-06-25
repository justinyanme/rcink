# launchd jobs

User-level launchd plists for the rcink producer.

## Claude / Codex credentials (read-only, no relogins)

The usage fetchers (`claude-fetch`, `codex-fetch`, `overview-fetch`,
`tlsr-push claude`, all reached via `fetch-all`) reuse the OAuth credential the
interactive `claude`/`codex` CLI maintains — the macOS Keychain item
`Claude Code-credentials` (or `~/.claude/.credentials.json` on Linux) and
`~/.codex/auth.json`. No env vars, no separate login.

They read these **read-only** and never refresh them: an OAuth refresh **rotates
the refresh token** and rewrites the Keychain item / `auth.json`, which would
invalidate the running CLI's credential and force a relogin. If a token is
missing or rejected the fetcher raises `ClaudeCredsError` / `CodexCredsError`;
`fetch-all` logs it and skips just that page (usage doesn't change while you're
idle anyway). Keep using the CLIs normally — they keep the tokens fresh and the
producer rides along.

## `com.rocry.tlsr-push.claude.plist`

Pushes the Claude usage page to the TLSR tag every 15 min. Relies on peek mode
in the firmware (`ENABLE_BLE_PEEK=ON`, default) so the tag is reachable on a
60-second cycle. Dedup means a push with unchanged usage skips without
connecting — cheap when the numbers haven't moved.

### Install

```bash
cp docs/launchd/com.rocry.tlsr-push.claude.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.tlsr-push.claude.plist
```

`RunAtLoad=true` so the first push fires immediately. Subsequent runs at
`StartInterval=900` s. Logs to `/tmp/tlsr-push.log`.

Claude OAuth comes from the macOS Keychain item `Claude Code-credentials`
(same as `claude-fetch`); no env vars needed.

### Uninstall / pause

```bash
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/com.rocry.tlsr-push.claude.plist
rm ~/Library/LaunchAgents/com.rocry.tlsr-push.claude.plist
```

### Verify it ran

```bash
tail -f /tmp/tlsr-push.log
# expect "tag already shows this exact content — skipping" most cycles,
# with a real push only when usage actually changed.
```

### Prerequisite — firmware peek mode

The tag must be running firmware built with `ENABLE_BLE_PEEK=ON` (the Makefile
default). Without peek, the tag only advertises for 5 min after a cold boot
then sleeps and disappears — scheduled pushes will all time out.
