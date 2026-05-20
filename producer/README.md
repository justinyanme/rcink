# tlsr-layout producer

Mac-side BLE producer for the TLSR8258 layout-DSL e-paper tag. See
`docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`.

## Status: Phase 1 (skeleton)

Built:
- Layout DSL builders (`tlsr_layout.layout`) — verbatim port of rcpapercolor's helpers
  with palette validation.
- Encode pipeline (`tlsr_layout.encode`) — canonical JSON → zlib → CRC32 → chunk frames.
- xxhash32 `data_ver` (`tlsr_layout.hash`) — matches the value the firmware advertises.
- Wire-format frame builders (`tlsr_layout.protocol`) — BEGIN/ABORT/COMMIT/DATA + ACK parser.
- CLI: `tlsr-layout dry-run <page> --out <dir>` writes JSON + zlib + chunk frames.

Not yet:
- `tlsr-layout devices` / `push` (Phase 6 — bleak BLE central).
- `tlsr-layout serve` daemon (Phase 7).
- Firmware (Phase 2+).

## Dev

```bash
cd producer
uv sync --all-extras
make test
make smoke
uv run tlsr-layout --help
```
