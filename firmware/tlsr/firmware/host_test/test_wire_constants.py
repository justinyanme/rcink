import json
import subprocess

from conftest import BIN


def test_wire_constants_dump_is_valid_json():
    out = subprocess.run([BIN, "wire-constants"], capture_output=True, check=True, text=True)
    data = json.loads(out.stdout)

    assert data["ctl"] == {"begin": 0x01, "abort": 0x02, "commit": 0x03}
    assert data["frame"]["len"] == 20
    assert data["frame"]["begin_off_data_ver"] == 1
    assert data["frame"]["begin_off_total_len"] == 5
    assert data["frame"]["begin_off_total_chunks"] == 7
    assert data["frame"]["begin_off_crc32"] == 9
    assert data["frame"]["begin_off_dsl_format"] == 13
    assert data["adv"]["low_batt"] == 0x02
    assert data["adv"]["xfer_err"] == 0x04
    assert data["limits"]["slot_header_size"] == 32
