# Carry Forward `data_ts` for Unchanged Page Content

Date: 2026-07-23
Status: Accepted

## Context

- PaperColor skips a panel refresh when the page ID and `data_ts` match the
  last rendered page.
- Its Axiom query accepts only page events with `_time > ago(1h)`.
- Producer runs must therefore publish fresh events even when page content has
  not changed.

## Decision

Treat `data_ts` as content-change time, not event-build time. Each producer run
ingests a fresh page event with a fresh `_time`; when its layout is unchanged,
the event carries forward that page's previous `data_ts`. New or changed
content receives a new `data_ts`.

## Alternative Considered

| Option | Result | Reason |
|---|---|---|
| Skip ingesting unchanged pages | Rejected | After one hour, unchanged pages fall outside PaperColor's `ago(1h)` query. If all pages are unchanged, the query returns no pages and firmware treats the fetch as failed. |

## Consequences

- + PaperColor continues to find every current page while avoiding
  pixel-identical panel refreshes.
- - Unchanged pages still consume one Axiom event per producer run.
- Revisit only if the firmware query no longer requires a fresh event inside
  its one-hour window.

## References

- [`producer/rcink/axiom.py`](../../producer/rcink/axiom.py)
- [`firmware/papercolor/include/config.h.example`](../../firmware/papercolor/include/config.h.example)
- [Issue #11](https://github.com/rocrp/rcink/issues/11)
