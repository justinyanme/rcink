import subprocess
import zlib
from pathlib import Path

from conftest import ROOT

BIN = ROOT / "build" / "host_test"


def test_uzlib_roundtrip(tmp_path: Path):
    payload = b'{"type":"text","value":"hi"}'
    compressed = zlib.compress(payload, level=9)
    in_path = tmp_path / "in.zlib"
    in_path.write_bytes(compressed)
    out = subprocess.run([BIN, "uzlib-test", str(in_path)], capture_output=True, check=True)
    assert out.stdout == payload
