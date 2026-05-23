# Axiom

The transport layer between the Mac producer and the M5PaperColor device. Every page builder serialises its layout tree to JSON, the producer POSTs one `rcpaper_page` event per page to a single dataset, and the firmware queries `latest-per-id` on each fetch. Sparklines pull from `rcpaper_metric` events written by the same job.

One token, one dataset — that's the whole config.

## Required env

| Var | Value |
|---|---|
| `AXIOM_TOKEN` | API token with **Ingest + Query** on the dataset |
| `AXIOM_DATASET` | Dataset name (this repo uses `rocry`) |

The firmware separately needs the same dataset + a token with **Query** in `src/config.h` (see top-level README "Firmware quickstart").

## Sign up + dataset

1. Create a free account at <https://app.axiom.co/> if you don't have one.
2. **Datasets → New dataset.** Name it `rocry` (or your variant — keep producer and firmware in sync).
3. Retention defaults to 30 days on the free tier — plenty for page events that get overwritten every 15 min anyway.

## Token

**Settings → API tokens → Create.** Two ways to scope it:

- **Personal token (`xaat-...`)** — read+write everywhere your account can reach. Easiest. Use this if it's only your device.
- **Dataset API token (`xapt-...`)** — least-privilege. Recommended for anything not pure-personal.
  - Permissions: **Ingest** + **Query** on the `rocry` dataset.

Either way, paste the secret once at creation — Axiom won't show it again.

## Wire-up

### One-shot manually

```bash
cd producer
export AXIOM_TOKEN=xaat-...
export AXIOM_DATASET=rocry
uv run fetch-all
```

`fetch-all` runs every page fetcher in one process and logs one summary line per page. A single failure (expired Codex auth, EventKit permissions revoked, GitHub token missing) degrades that page only; the rest still ship.

### Persistent via launchd

```bash
cp launchd/com.rocry.rcpaper.all.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
# fill in: absolute uv path, repo absolute path, AXIOM_TOKEN, AXIOM_DATASET,
# and (optionally) GITHUB_TOKEN + GITHUB_USERNAME for the github source.

launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
tail -f /tmp/rcpaper.log
```

`StartInterval` is 900 s (15 min). The first run fires immediately on load (`RunAtLoad`).

## Verification

```bash
# events the firmware will actually see
uv run preview --axiom overview      # pulls the latest overview event, renders HTML
uv run preview --axiom claude        # same for any other id
```

Quick sanity in the Axiom UI:

```axiom
['rocry']
| where ['type'] == 'rcpaper_page'
| summarize last_ts = max(_time) by id
```

You should see one row per page id (`overview`, `claude`, `codex`, `calendar`, `todo`, optionally `github`) updated within the last 15 minutes.

## Failure modes — what goes wrong, where it shows

| Symptom | Where it shows | Likely cause |
|---|---|---|
| `AxiomError: ingest HTTP 401` | producer log (`/tmp/rcpaper.log`) | token revoked / wrong env |
| `AxiomError: ingest HTTP 403` | producer log | token lacks Ingest on the dataset |
| `ingest failed: [...]` (HTTP 200) | producer log | event-schema mismatch — usually a layout dict with non-serialisable values; check the latest commit to `pages/` |
| Device shows STALE badge | device serial / panel | producer not running, or its events are >2× fetch interval old; check launchd `tail -f /tmp/rcpaper.log` |
| Device boots but shows no pages | device serial | wrong `AXIOM_DATASET` in `src/config.h` *or* the Axiom Root CA PEM is wrong / expired — see top-level README |
| All pages frozen on one event | Axiom UI | `bootout` the launchd job by mistake; reload with `bootstrap` |

## Notes on the schema

Each page event:

```json
{
  "_time": "2026-05-22T11:13:42.123456Z",
  "type": "rcpaper_page",
  "id": "overview",
  "order": 5,
  "data_ts": 1779440727,
  "layout_json": "{\"type\":\"vstack\",...}"
}
```

`layout_json` is **stringified**, not nested. Axiom otherwise flattens objects at ingest into `layout.children.0.type`-style columns, which (a) explodes the column count and (b) breaks the firmware's parse path. Keep it a string. See `rcpaper.axiom.ingest` for the source of truth.

`data_ts` is the producer-side wallclock at build time, exposed as epoch seconds so the firmware can display both *data HH:MM* (this value) and *refresh HH:MM* (its own `last_fetch_s`) without re-parsing `_time`. The "skip render if content unchanged" check on the device hinges on `data_ts`, so don't drop it.

`order` controls the on-device rotation: `overview=5, claude=10, codex=20, calendar=30, todo=40, github=50`. Renumber if you add new pages.

## Removing the producer

`launchctl bootout gui/$(id -u)/com.rocry.rcpaper.all` stops the cron. Existing events stay in Axiom until retention drops them; the device will show its last-known pages until they age past the firmware's stale threshold, after which the STALE pill appears in the bottom-right of the nav strip.
