# rcpaper — producer side

Pulls Claude/Codex usage and pushes layout-tree events to the `rcpapercolor`
Axiom dataset. Each script writes one event per run; the on-device firmware
queries the dataset every ~15 min.

## Setup

```bash
cd producer
uv sync
export AXIOM_TOKEN=xaat-...
export AXIOM_DATASET=rcpapercolor
uv run claude-fetch
uv run codex-fetch
```

See `launchd/` for example plists.
