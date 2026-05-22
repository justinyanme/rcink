from unittest.mock import AsyncMock, patch

from typer.testing import CliRunner

from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.ble.discovery import DiscoveredTag
from tlsr_layout.cli import app

from .fake_peripheral import FakePeripheral

runner = CliRunner()


def _tag(data_ver: int, device: object, *, flags: int = 0x01) -> DiscoveredTag:
    return DiscoveredTag(
        address="AA:BB:CC:DD:EE:FF",
        rssi=-42,
        adv=Advertisement(
            data_ver,
            2950,
            22,
            flags,
            bool(flags & 0x01),
            bool(flags & 0x02),
            bool(flags & 0x04),
            bool(flags & 0x08),
        ),
        device=device,
    )


def test_hardware_e2e_scans_pushes_status_and_verifies_post_adv():
    peripheral = FakePeripheral()
    device = object()

    async def fake_scan(*, duration_s: float):
        del duration_s
        if peripheral.data_ver == 0:
            return {"AA:BB:CC:DD:EE:FF": _tag(0xFFFFFFFF, device)}
        return {"AA:BB:CC:DD:EE:FF": _tag(peripheral.data_ver, device)}

    with (
        patch("tlsr_layout.cli.scan", fake_scan),
        patch("tlsr_layout.ble.transport.BleakClient", return_value=peripheral) as client_cls,
    ):
        result = runner.invoke(
            app,
            [
                "hardware-e2e",
                "example",
                "--device",
                "AA:BB:CC:DD:EE:FF",
                "--scan-duration",
                "0.01",
                "--post-scan-duration",
                "0.01",
            ],
        )

    assert result.exit_code == 0, result.output
    assert "before AA:BB:CC:DD:EE:FF data_ver=0xFFFFFFFF" in result.output
    assert "status_data_ver=0x" in result.output
    assert "hardware e2e ok: data_ver=0x" in result.output
    assert peripheral.data_ver != 0
    client_cls.assert_called_once_with(device, timeout=30.0)


def test_hardware_e2e_rejects_busy_post_adv():
    peripheral = FakePeripheral()
    device = object()

    async def fake_scan(*, duration_s: float):
        del duration_s
        if peripheral.data_ver == 0:
            return {"AA:BB:CC:DD:EE:FF": _tag(0xFFFFFFFF, device)}
        return {"AA:BB:CC:DD:EE:FF": _tag(peripheral.data_ver, device, flags=0x09)}

    with (
        patch("tlsr_layout.cli.scan", fake_scan),
        patch("tlsr_layout.ble.transport.BleakClient", return_value=peripheral),
    ):
        result = runner.invoke(
            app,
            [
                "hardware-e2e",
                "example",
                "--device",
                "AA:BB:CC:DD:EE:FF",
                "--scan-duration",
                "0.01",
                "--post-scan-duration",
                "0.01",
            ],
        )

    assert result.exit_code == 5
    assert "display idle and no render error not found" in result.output
    assert "flags=0x09" in result.output


def test_hardware_e2e_missing_device_exits_with_error():
    with patch("tlsr_layout.cli.scan", AsyncMock(return_value={})):
        result = runner.invoke(
            app,
            [
                "hardware-e2e",
                "example",
                "--device",
                "AA:BB:CC:DD:EE:FF",
                "--scan-duration",
                "0.01",
            ],
        )

    assert result.exit_code == 2
    assert "not found during scan" in result.output
