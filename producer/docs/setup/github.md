# GitHub source

Feeds two places:

- **`github` page** — its own card in the rotation (header + KPI row + 14-week daily contribution grid).
- **Overview header row** — `[GITHUB] N today · N week · top repo · Md streak`. Renders as `—` when this source is unconfigured, so overview never breaks because of it.

Uses one GraphQL query against `https://api.github.com/graphql`. No webhooks, no polling state.

## Required env

| Var | Value |
|---|---|
| `GITHUB_TOKEN` | Personal access token, see scopes below |
| `GITHUB_USERNAME` | Login whose activity to report on (e.g. `rocry`) |

## Token

Generate at <https://github.com/settings/tokens> → **Fine-grained personal access tokens** (preferred) or **Tokens (classic)**.

### Fine-grained (preferred)

- **Resource owner:** your user (or an org if you want org repos counted).
- **Repository access:** *All repositories* — visible repo/language detail only surfaces commits in repos the token can read. Private activity that cannot be broken down still appears through GitHub's contribution calendar.
- **Permissions:**
  - **Account → Profile:** *Read* (drives `user(login:)` resolution)
  - **Repository → Metadata:** *Read* (default; needed for visible repo/language detail)

No other scopes needed. The fewer the better — token can't write anything.

### Classic

If you go classic, the minimum is `read:user` + `repo` (the latter for private-repo contribution visibility).

Set expiration to whatever your rotation policy dictates. The producer fails clean when the token expires — see *Troubleshooting* below.

## Wire-up

### One-shot manually

```bash
cd producer
export AXIOM_TOKEN=xaat-...
export AXIOM_DATASET=rocry
export GITHUB_TOKEN=ghp-...               # or github_pat_...
export GITHUB_USERNAME=rocry
uv run github-fetch                       # standalone github page
uv run overview-fetch                     # overview header row picks up the github row
```

### Persistent via launchd

Edit `~/Library/LaunchAgents/com.rocry.rcpaper.all.plist` (the bundled job that already pushes claude / codex / calendar / todo / overview every 15 min) — append two entries to the `EnvironmentVariables` dict:

```xml
<key>GITHUB_TOKEN</key>
<string>ghp-...</string>
<key>GITHUB_USERNAME</key>
<string>rocry</string>
```

Reload:

```bash
launchctl bootout gui/$(id -u)/com.rocry.rcpaper.all
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.all.plist
tail -f /tmp/rcpaper.log
```

`com.rocry.rcpaper.all.plist.example` in this repo carries the same shape — diff against it when in doubt.

## Verification

After a successful fetch you should see something like:

```text
github @rocry: today=<N> week=<N> streak=<N>d repos=<N> 14w=<N>
fetch-all: 6 ok
```

Push to the device (or wait for the next 15-min tick):

1. Power-cycle the M5PaperColor.
2. Press `f` on the dev console (USB serial) — forces a refetch from Axiom.
3. **Overview** now shows the `GITHUB` row (between `CODEX` and the divider).
4. Next/Prev (`n` / `b`) until the `github` page appears: header `GITHUB · day-stamp`, `@<user>` line, three KPI cells, then the `CONTRIBS / DAY · 14W` contribution grid.

To inspect what got pushed:

```bash
uv run preview github           # build via the source + open HTML preview in the browser
uv run preview --axiom github   # pull the latest event out of Axiom and render it
```

## What gets reported

| Field | Source | Notes |
|---|---|---|
| `today_contributions` | latest `contributionCalendar` date bucket | This is GitHub profile contribution-graph count, not raw commits |
| `week_contributions` | sum of the last 7 `contributionCalendar` date buckets | Same contribution-graph caveat |
| `streak` | consecutive contribution-calendar days with `contributionCount > 0` (today=0 is allowed as a grace day) | Preserves the original GitHub calendar-day streak behavior |
| `total_14w` | sum of `daily_counts[98]` | Last 14 weeks × 7 contribution-calendar buckets |
| `total_year` | `contributionCalendar.totalContributions` | rolling 12 months, not calendar year |
| `daily_counts[98]` | `contributionCalendar.weeks[].contributionDays[].contributionCount` by date | Rendered directly as the contribution grid |

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `overview: github fetch failed: GITHUB_USERNAME not set in environment` | env var missing | Set both `GITHUB_TOKEN` and `GITHUB_USERNAME`; overview falls back to a `—` row |
| `GitHub GraphQL HTTP 401` | token revoked / expired | Mint a new token; replace in launchd plist; `bootout` + `bootstrap` |
| `GitHub GraphQL HTTP 403` | scopes too narrow (fine-grained) | Re-issue with the three permissions listed above |
| `GitHub user not found: <user>` | typo in `GITHUB_USERNAME` (case is preserved) or the account is suspended | Verify by visiting `https://github.com/<user>` |
| Visible repo/language detail is empty while counts are nonzero | GitHub exposes private activity as contribution-calendar counts without repo breakdown | Counts can still be right; repo detail requires GitHub to expose commit contributions for repos the token can read |
| Counts are lower than local `git log` activity | GitHub profile contributions only count pushed, contribution-eligible activity; local/unpushed commits and some branch/fork activity do not appear | Push/merge the relevant commits, or build a separate raw-local/repo-history source |
| Streak stuck at 0 | activity is in a non-personal org and the token doesn't cover it | Same fix |
| Overview shows `[GITHUB] —` even after configuring | overview pushes from `overview-fetch`, not `github-fetch` — overview-fetch runs the github source in-process, so a partial deploy can have one but not the other | Re-run `uv run overview-fetch` after editing env; or wait for the next launchd tick |
| `github` card never appears on the device | the standalone page only lands when `github-fetch` succeeds AND the device refetches | Verify via `uv run preview --axiom github`; if empty, run `uv run github-fetch` and press `f` on the dev console |

## Removing the source

Drop both env vars. `overview-fetch` will log a single `github fetch failed` warning and render the `—` row; the standalone github page won't be re-pushed (the last-known event in Axiom lingers until you delete it manually or it ages out of retention).
