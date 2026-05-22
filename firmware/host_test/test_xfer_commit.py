import subprocess

from conftest import BIN


def test_commit_success_switches_to_static_image_scene():
    out = subprocess.run(
        [BIN, "xfer-test", "commit-success-image-scene"],
        capture_output=True,
        check=True,
    )
    assert b"OK commit-success-image-scene" in out.stdout
