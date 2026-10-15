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

# (drive code, frames 1..255) on phase t0, or (code, frames, phase) with phase 1/2 using
# that phase's 2-bit code (raw level bits 1:0 / 7:6).
Step = tuple[int, int] | tuple[int, int, int]
# A repeated pair: (repeat 1..255, step A, step B) occupies one group.
Repeat = tuple[int, Step, Step]
Item = Step | Repeat


def _is_repeat(item: Item) -> bool:
    return isinstance(item[1], tuple)


def decode(wire: int) -> int:
    inv = ~wire & 0xFF
    return sum(((inv >> s) & 1) << i for i, s in enumerate(SRC))


def encode(value: int) -> int:
    inv = 0
    for i, s in enumerate(SRC):
        inv |= ((value >> i) & 1) << s
    return ~inv & 0xFF


RAW_SHIFT = {0: 2, 1: 0, 2: 6}  # phase -> raw inverted bit offset of its code
CODE_MAX = {0: 15, 1: 3, 2: 3}


def level(drive: int, phase: int = 0) -> int:
    """Decoded value of a level byte driving `drive` during phase t<phase>."""
    if not 0 <= drive <= CODE_MAX[phase]:
        raise ValueError(f"drive code {drive} for phase {phase}")
    return decode(~(drive << RAW_SHIFT[phase]) & 0xFF)


def _groups(items: list[Item]) -> list[tuple[int, Step, Step | None]]:
    out: list[tuple[int, Step, Step | None]] = []
    pending: list[Step] = []
    for item in items:
        if _is_repeat(item):
            if pending:
                out.append((1, pending[0], None))
                pending = []
            out.append(item)  # type: ignore[arg-type]
        else:
            pending.append(item)  # type: ignore[arg-type]
            if len(pending) == 2:
                out.append((1, pending[0], pending[1]))
                pending = []
    if pending:
        out.append((1, pending[0], None))
    return out


def row_values(items: list[Item]) -> list[int]:
    """Items -> decoded row values: each group = two steps (sets A, B) times a repeat."""
    groups = _groups(items)
    if len(groups) > GROUPS:
        raise ValueError(f"{len(groups)} groups > {GROUPS}")
    values: list[int] = []

    def half(step: Step) -> tuple[int, list[int]]:
        drive, n, phase = (*step, 0)[:3]
        if not 0 <= n <= 255:
            raise ValueError(f"frames out of range: {step}")
        counts = [0, 0, 0]
        counts[phase] = n
        return level(drive, phase), counts

    for repeat, a, b in groups:
        la, ca = half(a)
        lb, cb = half(b) if b else (level(DRIVE_NONE), [0, 0, 0])
        if not sum(ca) or not 1 <= repeat <= 255:
            raise ValueError(f"empty step or repeat out of range: {(repeat, a, b)}")
        values += [repeat, la, lb, *ca, 1, *cb, 1 if sum(cb) else 0]
    return values + [0] * (ROW - len(values))


def frames(items: list[Item]) -> int:
    total = 0
    for item in items:
        if _is_repeat(item):
            repeat, a, b = item  # type: ignore[misc]
            total += repeat * (a[1] + (b[1] if b else 0))
        else:
            total += item[1]  # type: ignore[index]
    return total


def right_align(rows: list[list[Item] | None]) -> list[list[Item]]:
    """Pad every row with leading idle frames so all rows end together on their own
    last drive. Idle after a drive lets the state relax (black 80 + idle 40 -> L 99)."""
    n = max(frames(r) for r in rows if r)
    out: list[list[Item]] = []
    for r in rows:
        left, pad = n - frames(r or []), []
        while left > 0:
            pad.append((DRIVE_NONE, min(255, left)))
            left -= min(255, left)
        out.append(pad + list(r or []))
    return out


def build(rows: list[list[Item]]) -> bytes:
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
