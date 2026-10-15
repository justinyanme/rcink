# /// script
# requires-python = ">=3.13"
# ///
"""ED2208 799-byte register LUT codec (decoded on PaperColor, 2026-10-15).

Wire byte = bit-inverted, bit-permuted value. Measured by BUSY timing:
value bit i comes from inverted wire bit SRC[i].
"""

import re
import sys
from pathlib import Path

SRC = (4, 7, 6, 1, 0, 3, 2, 5)  # value bit i <- (~wire) bit SRC[i]
PREFIX = 7
ROW = 88
ROWS = 9


def decode(wire: int) -> int:
    inv = ~wire & 0xFF
    return sum(((inv >> s) & 1) << i for i, s in enumerate(SRC))


def encode(value: int) -> int:
    inv = 0
    for i, s in enumerate(SRC):
        inv |= ((value >> i) & 1) << s
    return ~inv & 0xFF


Group = tuple[int, int, int, int, int, int]  # repeat, rails, rails2, count0, count1, count2


def rails(*phases: int) -> int:
    """Rails byte: phase k (0..3) selects voltage code phases[k] in bits 7-2k:6-2k."""
    return sum((code & 3) << (6 - 2 * k) for k, code in enumerate(phases))


def build(rows: list[list[Group]]) -> bytes:
    """Nine rows (row 0 = VCOM, row r = pixel code r-1) of groups -> 799 wire bytes.
    Each row ends with an empty repeat-1 group, as every TICKEY row does."""
    if len(rows) != ROWS:
        raise ValueError(f"need {ROWS} rows, got {len(rows)}")
    out = bytearray([0x00] * PREFIX)  # TICKEY prefix: wire 00 (decoded 255)
    for groups in rows:
        values = [v for g in groups for v in g] + [1]
        if len(values) > ROW or any(not 0 <= v <= 255 for v in values):
            raise ValueError(f"row overflow or value range: {groups}")
        out += bytes(encode(v) for v in values + [0] * (ROW - len(values)))
    return bytes(out)


def row_frames(groups: list[Group]) -> int:
    return sum(g[0] * (g[3] + g[4] + g[5]) for g in groups)


def lut_steps(lut: bytes, chunk: int = 32) -> list[str]:
    return [
        f"lut set {o} " + " ".join(f"{b:02x}" for b in lut[o : o + chunk]) for o in range(0, len(lut), chunk)
    ]


def load_inc(path: Path) -> dict[str, bytes]:
    text = path.read_text()
    return {
        m[1]: bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m[2]))
        for m in re.finditer(r"tickey_waveform_(\w+)\[TICKEY_WAVEFORM_BYTES\] = \{(.*?)\};", text, re.S)
    }


if __name__ == "__main__":
    assert all(decode(encode(v)) == v for v in range(256))
    _mono = load_inc(Path(__file__).resolve().parents[1] / "src" / "tickey_waveforms_mono.inc")["mono"]
    _g = [(1, 0, 0, 8, 0, 0)]
    assert build([_g, _g, [(1, 64, 0, 8, 0, 0)], [(1, 32, 0, 8, 0, 0)], *[_g] * 5]) == _mono
    src = Path(__file__).resolve().parents[1] / "src"
    tables = {**load_inc(src / "tickey_waveforms_mono.inc"), **load_inc(src / "tickey_waveforms_color.inc")}
    for name in sys.argv[1:] or tables:
        t = tables[name]
        print(f"== {name} prefix {[decode(b) for b in t[:PREFIX]]}")
        for r in range(ROWS):
            row = [decode(b) for b in t[PREFIX + r * ROW : PREFIX + (r + 1) * ROW]]
            while row and row[-1] == 0:
                row.pop()
            groups = [row[i : i + 6] for i in range(0, len(row), 6)]
            print(f"  row{r}: " + " | ".join(" ".join(f"{v:3d}" for v in g) for g in groups))
