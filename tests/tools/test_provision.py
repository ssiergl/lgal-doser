# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Values provision.py writes into the factory partition "fctry" (read by the firmware at start).
import pytest

import provision


@pytest.mark.parametrize("text,mask", [("1", 0b1), ("1,2,3", 0b111), ("5", 0b10000), (" 2 , 4 ", 0b1010)])
def test_parse_pumps(text, mask):
    assert provision.parse_pumps(text) == mask


@pytest.mark.parametrize("text", ["0", "6", "1,,2", "a", ""])
def test_parse_pumps_rejects(text):
    with pytest.raises(ValueError):
        provision.parse_pumps(text)


def test_pump_list():
    assert provision.pump_list(0b10101) == "1, 3, 5"


@pytest.mark.parametrize("text,rev", [("1.0", 10), ("1.1", 11), (" 2.3 ", 23)])
def test_parse_board_rev(text, rev):
    assert provision.parse_board_rev(text) == rev


@pytest.mark.parametrize("text", ["1", "1.10", "0.9", "v1.1"])
def test_parse_board_rev_rejects(text):
    with pytest.raises(ValueError):
        provision.parse_board_rev(text)


def test_factory_partition_same_in_all_tables():
    offset, size = provision.factory_partition()
    assert offset % 0x1000 == 0 and size >= 0x1000
