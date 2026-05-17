# rcpaper — producer side

Pulls Claude/Codex usage from the OAuth files those CLIs already maintain
(`~/.claude/.credentials.json`, `~/.codex/auth.json`), builds a layout-tree
page, and pushes one event per run to the `rcpapercolor` Axiom dataset.

## One-time setup

1. Install `uv` (`brew install uv`) if you don't have it.
2. `cd producer && uv sync`
3. Get an Axiom ingest+query token from <https://axiom.co/settings/api-tokens>.
4. Smoke-test manually:

   ```bash
   export AXIOM_TOKEN=xaat-...
   export AXIOM_DATASET=rcpapercolor
   uv run claude-fetch
   uv run codex-fetch
   ```

   You should see two `kind=page` events appear in the Axiom dataset.

## Schedule with launchd

```bash
# Copy and edit
cp launchd/com.rocry.rcpaper.claude.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
cp launchd/com.rocry.rcpaper.codex.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Fix `CHANGE_ME` paths + AXIOM_TOKEN in both files
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Load
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Tail logs
tail -f /tmp/rcpaper-{claude,codex}.log
```

## Adding a new dashboard

1. Add a source under `rcpaper/sources/<name>.py` (returns a dataclass).
2. Add a page builder under `rcpaper/pages/<name>.py` (takes the dataclass,
   returns a layout dict).
3. Add an entry script in `scripts/<name>_fetch.py` that wires source → page →
   `ingest(id="<name>", layout=..., order=N)`.
4. Add a launchd plist if you want it scheduled.

The firmware will pick up the new page automatically on its next fetch (the
APL query returns latest-per-id).
