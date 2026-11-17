# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Must give the same values as the firmware (tests/host/test_auth.cpp, test_derive): the label
# printed at provisioning and the passwords the device accepts come from the same formula.
import pytest

import ap_password

MAC = bytes([0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5])
PROVISIONED = "0ba9da55f69e9b4e63d0919fd75787aeec5077944585eff083bb3448e97909bb"


def test_device_secret_from_master():
    assert ap_password.device_secret("test-master-secret", MAC) == PROVISIONED


@pytest.mark.parametrize("secret,label,chars,want", [
    ("test-secret", b"LGAL-AP", 12, "jr5rf4tqe4ax"),
    ("test-secret", b"LGAL-ENG", 16, "nufkq664m949efry"),
    (PROVISIONED, b"LGAL-AP", 12, "79ng72jc87k4"),
    (PROVISIONED, b"LGAL-ENG", 16, "brd7jfepbw37w8u4"),
])
def test_derive_matches_firmware(secret, label, chars, want):
    assert ap_password.derive(MAC, secret, label, chars) == want


@pytest.mark.parametrize("text", ["A0:B1:C2:D3:E4:F5", "a0-b1-c2-d3-e4-f5", "A0B1C2D3E4F5"])
def test_parse_mac(text):
    assert ap_password.parse_mac(text) == MAC


@pytest.mark.parametrize("text", ["A0:B1:C2:D3:E4", "A0:B1:C2:D3:E4:F5:00", ""])
def test_parse_mac_rejects(text):
    with pytest.raises(SystemExit):
        ap_password.parse_mac(text)


def test_master_too_short(tmp_path):
    f = tmp_path / "master"
    f.write_text("short\n")
    with pytest.raises(SystemExit):
        ap_password.read_master(f)
