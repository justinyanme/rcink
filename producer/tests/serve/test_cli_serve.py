from unittest.mock import AsyncMock, patch

from typer.testing import CliRunner

from tlsr_layout.cli import app
from tlsr_layout.serve import ServeConfig

runner = CliRunner()


def test_cli_serve_runs_daemon_with_config():
    with patch("tlsr_layout.cli.serve_daemon", new=AsyncMock()) as serve:
        result = runner.invoke(
            app,
            [
                "serve",
                "--device-filter",
                "AA:BB:CC:DD:EE:FF",
                "--page",
                "claude",
                "--page",
                "todo",
                "--log-level",
                "DEBUG",
            ],
        )

    assert result.exit_code == 0, result.output
    serve.assert_awaited_once()
    config = serve.await_args.args[0]
    assert config == ServeConfig(
        device_filter="AA:BB:CC:DD:EE:FF",
        rotation=["claude", "todo"],
    )
