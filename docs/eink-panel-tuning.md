# E-ink panel tuning: what worked for us

Experience report, not a recipe. One panel, three days (2026-10-13..16): M5Stack PaperColor,
EL040EF1 400×600 six-colour (Spectra-6 class), ED2208 controller, SPI, RAM-loadable 799-byte LUT.
Result: own waveforms for mono (3.2 s, residue ≈ native), six colours (4.4 s vs native 14.4 s)
and 48 distinguishable colours. Details and evidence: `firmware/papercolor-lab/README.md`.
Terms (Native refresh, Fast mono, Clean mono, Cleanup, Trial, Own waveform): `CONTEXT.md`.

Below: only what we would insist on again, then what we did and where it may not transfer.
Your panel, controller and judgement may well find a better route.

## Rules we would keep

1. **Native baseline first.** Photograph and time the factory refresh with the same camera,
   framing and metric before anything else. Every later claim is relative to it.
2. **Nothing permanent.** No OTP/NVM writes. Volatile registers only; voltage/power registers
   last, if ever. End every experiment with a Native refresh (Cleanup) so the panel is known.
3. **Separate Lab firmware.** Raw driver + line console: boot idle, one command → one reply
   line (`OK k=v …` / `FAIL stage=…`), fail loud, no retries. The host owns the experiment.
   Product code and libraries hide exactly the bytes you need to see.
4. **One Trial = one directory**: serial log, timestamped commands, photos. Change one variable
   per Trial. Curate small evidence (JPEG ≤ 200 KB + log) into the repo next to the README line
   that cites it; keep the bulk in a cache dir.
5. **The camera is the oracle; BUSY is the clock.** A phone camera (rceye) driven by the host
   script after every step. Look at the photo before trusting a number.
6. **Re-test before believing.** Camera colour is noisy and depends on room light and on what
   else is on screen. Accept only repeated means. A hill climb on a noisy count over-selects
   (our "51 colours" re-tested as 44).
7. **Normalise light.** Compare within one photo, or scale each channel so a reference patch in
   the same photo matches a fixed value. Note the time of day.
8. **Drive every pixel for clean results** (ghost rule). A pixel that idles while others are
   driven keeps showing the previous image. Fast updates may idle unchanged pixels; anything
   that must be ghost-free drives every pixel through the whole final pass.
9. **Measure over time, not only right after the refresh.** Ink can relax or fade minutes later
   (open: rcink#23). Repeat updates (we used 60–120) to catch drift.

## What we did, in order

1. Native baseline (#19).
2. Reproduce known working references on the same panel (FreeInk interrupted-OTP mono #20,
   TICKEY LUT port #21/#22). This found setup and frame-format bugs (e.g. 2 bpp with every row
   sent twice) that would otherwise have looked like bad waveforms.
3. Decode the LUT format (below), then write own waveforms: borrow structure from references,
   never their bytes.
4. Small demo firmware (Demo gallery) so a human judges results on the device.

## Decoding an unknown LUT

- Time BUSY while changing one byte: frame counts and repeat counts reveal themselves; one
  frame ≈ BUSY / total frames.
- Drive one LUT row with a solid fill and photograph: maps drive codes to colours.
- Put the format in a codec script with a round-trip check (`tools/lutcodec.py`); write LUTs
  as step lists (code × frames), never hand hex. Equal-length rows, right-aligned, so every
  row ends on its own last drive.
- Generate firmware tables from the same scripts (`tools/gen_demo_luts.py`) and fail the build
  check when they drift.

## Panel-specific findings (verify on yours)

- A register bit chose the frame format (bits per pixel, rows covered); the wrong one silently
  stopped LUT frames at row ~491.
- Single-bit drive codes were stable; multi-bit codes changed with what other pixels drove.
- Colour = how an alternating pulse train ends: the last pulse (and its length) decides.
- More colours than pigments: two passes, each pixel's value split into fields that pick a
  base train and a short modifier (8 × 8 classes → 48–50 distinct at ΔE 5).
- Frame clock was a register (7.6–30 ms/frame); faster did not help colour.

## Other panels

- **Mono / 3–4 colour SPI controllers with LUT RAM** (SSD16xx, UC81xx, …): same flow; fewer
  pigments make colour trains simpler; rules 1–9 apply unchanged.
- **Parallel panels** (epdiy style, MCU drives source/gate per frame): untested by us. The host
  is the waveform engine: our "LUT row" becomes your per-pixel state machine, "frame clock" your
  scan period; the measurement rules transfer. Prior art studied statically, no panel runs:
  - `~/w/_hw/rcwodle/docs/research/edcbook/` (rocrp/rcwodle): epdiy waveforms and refresh flow
    (parallel), SSD1677 temporal 16-level grey (multi-pass, two RAM planes) and phase-banded
    page-turn animation over SPI.
  - `~/w/_hw/rcwodle/docs/research/whiteos/`: ripple animation by phase scheduling, waveform data.
- Open references: FreeInk SDK, epdiy, TICKEY (sf32rc), M5GFX panel drivers.

## Reusable tools (firmware/papercolor-lab)

| Tool | Use |
| --- | --- |
| `lab.py trial/flash/curate` | one Trial: console session + camera photos + curation |
| `tools/lutcodec.py` | LUT step lists ↔ bytes (ED2208 format; template for others) |
| `tools/ghost_metric.py` | residue: high-pass energy of a flat area |
| `tools/white_spread.py` | colour history left under white |
| `tools/own_grid.py` `largest_distinct` | how many patches are pairwise ΔE-distinct |

## Open

Fast mono ink fading and ghost build-up (rcink#23); temperature dependence untested; long-term
panel wear from own waveforms unknown.
