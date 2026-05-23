from unittest.mock import AsyncMock, patch

from typer.testing import CliRunner

from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.ble.discovery import DiscoveredTag
from tlsr_layout.cli import app

from .fake_peripheral import FakePeripheral

runner = CliRunner()


def _tag(device):
    return DiscoveredTag(
        address="AA:BB:CC:DD:EE:FF",
        rssi=-42,
        adv=Advertisement(0xFFFFFFFF, 2950, 22, 0x01, True, False, False),
        device=device,
    )


def test_push_writes_page_to_fake_peripheral():
    peripheral = FakePeripheral()
    device = object()

    with (
        patch("tlsr_layout.cli.scan", AsyncMock(return_value={"AA:BB:CC:DD:EE:FF": _tag(device)})),
        patch("tlsr_layout.ble.transport.BleakClient", return_value=peripheral) as client_cls,
    ):
        result = runner.invoke(
            app,
            ["push", "example", "--device", "AA:BB:CC:DD:EE:FF"],
        )

    assert result.exit_code == 0, result.output
    assert "push ok: data_ver=0x" in result.output
    assert peripheral.data_ver != 0
    client_cls.assert_called_once_with(device, timeout=30.0)


def test_push_unknown_page_exits_with_error():
    result = runner.invoke(
        app,
        ["push", "missing", "--device", "AA:BB:CC:DD:EE:FF"],
    )

    assert result.exit_code == 2
    assert "unknown page" in result.output


def test_push_low_battery_exits_with_error():
    with (
        patch(
            "tlsr_layout.cli.scan", AsyncMock(return_value={"AA:BB:CC:DD:EE:FF": _tag(object())})
        ),
        patch(
            "tlsr_layout.ble.transport.BleakClient",
            return_value=FakePeripheral(forced_low_batt=True),
        ),
    ):
        result = runner.invoke(
            app,
            ["push", "example", "--device", "AA:BB:CC:DD:EE:FF"],
        )

    assert result.exit_code == 3
    assert "low battery" in result.output


def test_push_missing_device_exits_with_error():
    with patch("tlsr_layout.cli.scan", AsyncMock(return_value={})):
        result = runner.invoke(
            app,
            ["push", "example", "--device", "AA:BB:CC:DD:EE:FF", "--scan-duration", "0.01"],
        )

    assert result.exit_code == 2
    assert "not found during scan" in result.output
