# rcink — agent guide

This is an umbrella repo for multi-device e-ink dashboards. Six firmware targets share one Python producer and one DSL.

## Where things are

- DSL contract: `dsl/SPEC.md`, `dsl/schema.json`. Read these before touching producer or any firmware.
- Producer: `producer/rcink/` — Python ≥3.13, uv-managed. Run with `uv run rcink ...`.
- Firmwares: each in `firmware/<device>/` with its own build system. Do not unify.
- Lab firmware for PaperColor panel Trials: `firmware/papercolor-lab/` (raw driver + console, no M5 libs; see its README).
- E-ink tuning experience report (reusable for other panels): `docs/eink-panel-tuning.md`.
- Specs & plans: `docs/superpowers/{specs,plans}/`.

## Per-firmware references

Each firmware subdir should keep target-specific operator notes in its
`README.md`; read the matching firmware README before changing that target.

## Color tokens

DSL accepts both semantic (`accent`, `warning`, `success`, `info`, `muted`) and concrete (`red`, `yellow`, `green`, `blue`, `black`, `white`) tokens. Each firmware maps both to its physical palette. New page code SHOULD use semantic; legacy concrete remains supported.
