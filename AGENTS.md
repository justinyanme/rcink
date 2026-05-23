# rcink — agent guide

This is an umbrella repo for multi-device e-ink dashboards. Three firmware targets share one Python producer and one DSL.

## Where things are

- DSL contract: `dsl/SPEC.md`, `dsl/schema.json`. Read these before touching producer or any firmware.
- Producer: `producer/rcink/` — Python ≥3.13, uv-managed. Run with `uv run rcink ...`.
- Firmwares: each in `firmware/<device>/` with its own build system. Do not unify.
- Specs & plans: `docs/superpowers/{specs,plans}/`.

## Per-firmware AGENTS files

Each firmware subdir has its own AGENTS.md from before the merge:

- `firmware/s3/AGENTS.md` — M5 PaperColor specifics
- `firmware/tlsr/AGENTS.md` — TLSR8258 / TC32 specifics

Read those when working on a specific device.

## Color tokens

DSL accepts both semantic (`accent`, `warning`, `success`, `info`, `muted`) and concrete (`red`, `yellow`, `green`, `blue`, `black`, `white`) tokens. Each firmware maps both to its physical palette. New page code SHOULD use semantic; legacy concrete remains supported.
