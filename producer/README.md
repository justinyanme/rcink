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
- **Image** — one-off remote image URL pages pushed manually with `uv run image-fetch ...`.

## One-time setup

1. Install `uv` (`brew install uv`) if you don't have it.
2. `cd producer && uv sync`
3. Get an Axiom ingest+query token from <https://axiom.co/settings/api-tokens>.
4. Smoke-test manually:

   ```bash
   export AXIOM_TOKEN=xaat-...
   export AXIOM_DATASET=rcpapercolor
   uv run fetch-all
   uv run image-fetch 'https://example.com/frame.png'   # optional, ad-hoc image push
   ```

   `fetch-all` runs every page fetcher in one process, logging one line per
   page; individual failures degrade that page only. Each invocation should
   write 6 `type=rcpaper_page` events to the Axiom dataset.

## Schedule with launchd

One job runs the whole rotation every 15 minutes:

```bash
cp launchd/com.rocry.rcpaper.all.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist   # fill in user, uv path, AXIOM_TOKEN

launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
tail -f /tmp/rcpaper.log
```

The calendar + todo fetchers read from macOS Calendar/Reminders via EventKit.
On first launch macOS prompts to grant Calendar and Reminders access to the
`uv`/`python` binary; grant it. The default Reminders list (System Settings →
Reminders → Default List) is the one rendered.

Need a different cadence for one page, or want to skip one? Add or remove its
entry in `scripts/fetch_all.py:FETCHERS`, or call the per-page script
(`uv run claude-fetch`, etc.) from a separate job.

## Adding a new dashboard

1. Add a source under `rcpaper/sources/<name>.py` (returns a dataclass).
2. Add a page builder under `rcpaper/pages/<name>.py` (takes the dataclass,
   returns a layout dict).
3. Add an entry script in `scripts/<name>_fetch.py` that wires source → page →
   `ingest(id="<name>", layout=..., order=N)`.
4. Register it in `scripts/fetch_all.py:FETCHERS` so the unified job picks it up.

The firmware will pick up the new page automatically on its next fetch (the
APL query returns latest-per-id).
