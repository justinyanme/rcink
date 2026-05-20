from unittest.mock import patch

from typer.testing import CliRunner

from tlsr_layout.cli import app

from .fake_peripheral import FakePeripheral

runner = CliRunner()


def test_push_writes_page_to_fake_peripheral():
    peripheral = FakePeripheral()

    with patch("bleak.BleakClient", return_value=peripheral) as client_cls:
        result = runner.invoke(
            app,
            ["push", "example", "--device", "AA:BB:CC:DD:EE:FF"],
        )

    assert result.exit_code == 0, result.output
    assert "push ok: data_ver=0x" in result.output
    assert peripheral.data_ver != 0
    client_cls.assert_called_once_with("AA:BB:CC:DD:EE:FF")


def test_push_unknown_page_exits_with_error():
    result = runner.invoke(
        app,
        ["push", "missing", "--device", "AA:BB:CC:DD:EE:FF"],
    )

    assert result.exit_code == 2
    assert "unknown page" in result.output


def test_push_low_battery_exits_with_error():
    with patch("bleak.BleakClient", return_value=FakePeripheral(forced_low_batt=True)):
        result = runner.invoke(
            app,
            ["push", "example", "--device", "AA:BB:CC:DD:EE:FF"],
        )

    assert result.exit_code == 3
    assert "low battery" in result.output
