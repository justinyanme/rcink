from unittest.mock import AsyncMock, patch

from typer.testing import CliRunner

from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.ble.discovery import DiscoveredTag
from tlsr_layout.cli import app

runner = CliRunner()


def test_devices_lists_tags():
    fake = {
        "AA:BB:CC:DD:EE:FF": DiscoveredTag(
            address="AA:BB:CC:DD:EE:FF",
            rssi=-40,
            adv=Advertisement(0xDEADBEEF, 2950, 22, 0x01, True, False, False),
        )
    }

    with patch("tlsr_layout.cli.scan", AsyncMock(return_value=fake)) as scan:
        result = runner.invoke(app, ["devices", "--duration", "0.01"])

    assert result.exit_code == 0, result.output
    assert "AA:BB:CC:DD:EE:FF" in result.output
    assert "rssi=-40" in result.output
    assert "data_ver=0xDEADBEEF" in result.output
    assert "batt=2950mV" in result.output
    scan.assert_awaited_once_with(duration_s=0.01)


def test_devices_prints_empty_state():
    with patch("tlsr_layout.cli.scan", AsyncMock(return_value={})):
        result = runner.invoke(app, ["devices", "--duration", "0"])

    assert result.exit_code == 0, result.output
    assert "no tags found" in result.output
