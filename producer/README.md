# rcpaper — producer side

Builds dashboard pages and pushes one event per run to the `rcpapercolor`
Axiom dataset. The firmware reads them back at the next fetch interval.

Sources:
- **Claude / Codex** — usage via the OAuth files those CLIs already maintain
  (`~/.claude/.credentials.json`, `~/.codex/auth.json`).
- **Calendar** — today's events from all macOS Calendars (EventKit).
- **Todo** — incomplete reminders from the default macOS Reminders list
  (EventKit, async fetch pumped synchronously via NSRunLoop).
- **Quote** — one of a curated set, rotated by date ordinal.

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
# Copy and edit (one plist per fetcher)
for kind in claude codex calendar todo; do
  cp launchd/com.rocry.rcpaper.${kind}.plist.example \
     ~/Library/LaunchAgents/com.rocry.rcpaper.${kind}.plist
  $EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.${kind}.plist
done

# Load
for kind in claude codex calendar todo; do
  launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.${kind}.plist
done

# Tail logs
tail -f /tmp/rcpaper-{claude,codex,calendar,todo}.log
```

The `calendar-fetch` + `todo-fetch` jobs read from macOS Calendar/Reminders via
EventKit. On first launch macOS prompts to grant Calendar and Reminders access
to the `uv`/`python` binary; grant it. The default Reminders list (System
Settings → Reminders → Default List) is the one rendered.

## Adding a new dashboard

1. Add a source under `rcpaper/sources/<name>.py` (returns a dataclass).
2. Add a page builder under `rcpaper/pages/<name>.py` (takes the dataclass,
   returns a layout dict).
3. Add an entry script in `scripts/<name>_fetch.py` that wires source → page →
   `ingest(id="<name>", layout=..., order=N)`.
4. Add a launchd plist if you want it scheduled.

The firmware will pick up the new page automatically on its next fetch (the
APL query returns latest-per-id).
