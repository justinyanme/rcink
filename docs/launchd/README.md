# launchd jobs

User-level launchd plists for the rcink producer.

## Claude credentials & relogin avoidance

The Claude usage fetchers (`claude-fetch`, `overview-fetch`, `tlsr-push claude`,
all reached via `fetch-all`) share one OAuth credential with the interactive
`claude` CLI — the macOS Keychain item `Claude Code-credentials` (or, on Linux,
`~/.claude/.credentials.json`).

That credential is read **read-only**. The producer never refreshes it, because
an OAuth refresh **rotates the refresh token** and rewrites the Keychain item —
which invalidates the credential the running `claude` CLI holds and forces a
relogin. When the shared token has expired, the fetcher raises
`ClaudeTokenExpired`; `fetch-all` logs it and skips that page (usage doesn't
change while you're idle anyway), and it recovers the next time you use `claude`.

For uninterrupted 24/7 updates, give the producer its **own independent**
credential and set `RCINK_CLAUDE_CREDS` to its path:

```bash
# Mint a long-lived, independent OAuth token (separate grant; does NOT touch the
# Keychain item the interactive CLI uses):
claude setup-token            # prints sk-ant-oat01-...  (interactive, ~1 yr)

# Store it where only the producer reads it, in the shape the fetcher expects.
# expiresAt is set far in the future so the producer never tries to refresh it.
python3 - "$(pbpaste)" <<'PY'   # or paste the token in place of $(pbpaste)
import json, sys, time, os
tok = sys.argv[1].strip()
p = os.path.expanduser("~/.claude/.rcink-producer-creds.json")
json.dump({"claudeAiOauth": {
    "accessToken": tok, "refreshToken": "",
    "expiresAt": int((time.time() + 365*86400) * 1000),
    "scopes": ["user:profile", "user:inference"],
}}, open(p, "w"))
os.chmod(p, 0o600); print("wrote", p)
PY
```

Then add to the job's plist (see the `EnvironmentVariables` block in
`com.rocry.rcpaper.all.plist`):

```xml
<key>RCINK_CLAUDE_CREDS</key>
<string>/Users/&lt;you&gt;/.claude/.rcink-producer-creds.json</string>
```

Because that file is a separate grant, the producer refreshing/using it never
disturbs the interactive CLI's login.

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
