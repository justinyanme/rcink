# tlsr-layout producer

Mac-side BLE producer for the TLSR8258 layout-DSL e-paper tag. See
`docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`.

## Status

- [x] Phase 1 - producer skeleton
- [x] Phase 2-5 - firmware render/transfer foundation
- [x] Phase 6 - BLE central (`devices`, `push`)
- [x] Phase 7 - serve daemon + page registry (`claude`, `codex`, `calendar`, `todo`)

## Daemon

```bash
tlsr-layout serve
tlsr-layout serve --device-filter AA:BB:CC:DD:EE:FF
tlsr-layout serve --page claude --page todo
```

State persists at `~/.config/tlsr-layout/state.json`. Set
`TLSR_LAYOUT_CONFIG_DIR` to override this path for tests or local experiments.

## Pages

Registered pages:

- `example` - fixture/dashboard smoke page
- `claude` - Claude usage quota, compact B/W/R layout
- `codex` - Codex usage quota, compact B/W/R layout
- `calendar` - next calendar event from macOS EventKit
- `todo` - top reminders from macOS EventKit

## Dev

```bash
cd producer
uv sync --all-extras
make test
make smoke
uv run tlsr-layout --help
```
