import json
import zlib
from pathlib import Path

from typer.testing import CliRunner

from tlsr_layout.cli import app

runner = CliRunner()


def test_dry_run_writes_artifacts(tmp_path: Path):
    out = tmp_path / "out"
    result = runner.invoke(app, ["dry-run", "example", "--out", str(out)])
    assert result.exit_code == 0, result.output

    canonical = out / "example.json"
    compressed = out / "example.zlib"
    chunks = out / "example.chunks.bin"
    summary = out / "example.summary.json"

    for p in (canonical, compressed, chunks, summary):
        assert p.exists(), f"expected {p}"

    parsed = json.loads(canonical.read_text())
    assert parsed["type"] == "vstack"

    raw = canonical.read_bytes()
    assert zlib.decompress(compressed.read_bytes()) == raw

    info = json.loads(summary.read_text())
    assert info["page"] == "example"
    assert info["uncompressed_len"] == len(raw)
    assert info["compressed_len"] == len(compressed.read_bytes())
    assert info["chunks"] >= 1
    assert "data_ver" in info
    assert "crc32" in info


def test_dry_run_unknown_page(tmp_path: Path):
    result = runner.invoke(app, ["dry-run", "nope", "--out", str(tmp_path)])
    assert result.exit_code != 0
    assert "unknown page" in result.output.lower()


def test_devices_stub():
    result = runner.invoke(app, ["devices"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()


def test_push_stub():
    result = runner.invoke(app, ["push", "example"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()


def test_serve_stub():
    result = runner.invoke(app, ["serve"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()
