# /// script
# requires-python = ">=3.13"
# ///
"""ED2208 799-byte register LUT codec, decoded on PaperColor (EL040EF1) 2026-10-15.

Layout (all measured with BUSY timing and photos, see README "LUT format"):
- 7 prefix bytes (wire 00), then 9 rows x 88 bytes. Row 0 drives VCOM, row r drives
  pixel transition code r-1 (mono: (old<<1)|new; colour: 3-bit selector).
- A row is up to 8 groups of 11 bytes:
  [group repeat][level A][level B][A t0][A t1][A t2][A repeat][B t0][B t1][B t2][B repeat].
  A group with no frames ends the row.
- Wire byte = bit-inverted value, bits permuted (SRC below) for every byte.
- Level byte, raw inverted bits: 5:2 = drive code during t0 (DRIVE_*), 1:0 = during t1,
  7:6 = during t2. One frame ~11.7 ms with TICKEY setup (30 04).
"""

import re
import sys
from pathlib import Path

SRC = (4, 7, 6, 1, 0, 3, 2, 5)  # value bit i <- (~wire) bit SRC[i]
PREFIX = 7
ROW = 88
ROWS = 9
GROUP = 11
GROUPS = ROW // GROUP

# Phase-0 drive codes (raw inverted level bits 5:2), measured from native white/black.
DRIVE_NONE = 0
DRIVE_YELLOW = 1
DRIVE_BLACK = 2
DRIVE_WHITE = 8

Step = tuple[int, int]  # (drive code, frames 1..255)


def decode(wire: int) -> int:
    inv = ~wire & 0xFF
    return sum(((inv >> s) & 1) << i for i, s in enumerate(SRC))


def encode(value: int) -> int:
    inv = 0
    for i, s in enumerate(SRC):
        inv |= ((value >> i) & 1) << s
    return ~inv & 0xFF


def level(drive: int) -> int:
    """Decoded value of a level byte driving `drive` during t0."""
    if not 0 <= drive <= 15:
        raise ValueError(f"drive code {drive}")
    return decode(~(drive << 2) & 0xFF)


def row_values(steps: list[Step]) -> list[int]:
    """Steps -> decoded row values: two steps per group (sets A, B), phase t0 only."""
    if len(steps) > 2 * GROUPS:
        raise ValueError(f"{len(steps)} steps > {2 * GROUPS}")
    values: list[int] = []
    for i in range(0, len(steps), 2):
        (da, na), (db, nb) = steps[i], steps[i + 1] if i + 1 < len(steps) else (DRIVE_NONE, 0)
        if not (1 <= na <= 255 and 0 <= nb <= 255):
            raise ValueError(f"frames out of range: {steps[i : i + 2]}")
        values += [1, level(da), level(db), na, 0, 0, 1, nb, 0, 0, 1 if nb else 0]
    return values + [0] * (ROW - len(values))


def frames(steps: list[Step]) -> int:
    return sum(n for _, n in steps)


def build(rows: list[list[Step]]) -> bytes:
    """Nine step lists (row 0 = VCOM) -> 799 wire bytes. All rows must last equally long."""
    if len(rows) != ROWS:
        raise ValueError(f"need {ROWS} rows, got {len(rows)}")
    if len({frames(r) for r in rows}) != 1:
        raise ValueError(f"rows differ in length: {[frames(r) for r in rows]}")
    return bytes([0x00] * PREFIX) + b"".join(bytes(encode(v) for v in row_values(r)) for r in rows)


def lut_steps(lut: bytes, chunk: int = 32) -> list[str]:
    return [f"lut set {o} " + " ".join(f"{b:02x}" for b in lut[o : o + chunk]) for o in range(0, len(lut), chunk)]


def load_inc(path: Path) -> dict[str, bytes]:
    text = path.read_text()
    return {
        m[1]: bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m[2]))
        for m in re.finditer(r"tickey_waveform_(\w+)\[TICKEY_WAVEFORM_BYTES\] = \{(.*?)\};", text, re.S)
    }


if __name__ == "__main__":
    assert all(decode(encode(v)) == v for v in range(256))
    src = Path(__file__).resolve().parents[1] / "src"
    tables = {**load_inc(src / "tickey_waveforms_mono.inc"), **load_inc(src / "tickey_waveforms_color.inc")}
    for name in sys.argv[1:] or tables:
        t = tables[name]
        print(f"== {name}")
        for r in range(ROWS):
            row = [decode(b) for b in t[PREFIX + r * ROW : PREFIX + (r + 1) * ROW]]
            groups = [row[i : i + GROUP] for i in range(0, ROW, GROUP) if any(row[i : i + GROUP])]
            print(f"  row{r}: " + " | ".join(" ".join(f"{v:3d}" for v in g) for g in groups))
