"""The symbolizer's routing, which is the part that can be silently wrong.

Running addr2line is not interesting; choosing the wrong ELF to run it against
is, because it answers confidently either way. A kernel address explained by
the ROM's symbols reads exactly like a correct answer.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from symbolize import addresses_in, describe, parse_addr2line, select_image


def test_each_image_claims_its_own_range():
    assert select_image(0x00100176)[0] == "user"
    assert select_image(0x0205A29C)[0] == "kernel"
    assert select_image(0xFFE00400)[0] == "rom"


def test_boundaries_belong_to_the_image_that_starts_there():
    assert select_image(0x00100000)[0] == "user"
    assert select_image(0x02044000)[0] == "kernel"
    assert select_image(0xFFE00000)[0] == "rom"


def test_addresses_outside_every_image_are_not_guessed_at():
    """A stack address is not code, and pretending otherwise misleads."""
    assert select_image(0x7000CF98) is None
    assert select_image(0x00000000) is None
    assert select_image(0x02000000) is None      # kernel RAM below the image
    assert select_image(0x001FFFFF + 1) is None  # one past the user image


def test_an_unclaimed_address_is_reported_as_such(tmp_path):
    line = describe(0x7000CF98, str(tmp_path))
    assert "0x7000cf98" in line
    assert "no image covers" in line


def test_a_missing_elf_is_said_rather_than_silently_skipped(tmp_path):
    line = describe(0x00100176, str(tmp_path))
    assert "is not built" in line


def test_addr2line_output_is_parsed_in_both_shapes():
    assert parse_addr2line("kernel_process_on_fault\nprocess.c:5109\n") == (
        "kernel_process_on_fault", "process.c:5109")
    assert parse_addr2line("??\n??:0\n") == ("??", "??:0")
    assert parse_addr2line("only_a_function\n") == ("only_a_function", "??")
    assert parse_addr2line("") == ("??", "??")


def test_addresses_are_scraped_out_of_a_report():
    report = (
        "*** user fault: process 0x10000011 thread 0x20000005\n"
        "    pc 0x00100176  address 0x7000cf98  vector 2\n")
    assert addresses_in(report) == [0x10000011, 0x20000005, 0x00100176,
                                    0x7000CF98]


def test_short_hex_is_not_mistaken_for_an_address():
    """Vector numbers and flags appear as small hex and are not addresses."""
    assert addresses_in("vector 0x2 flags 0x10") == []
    assert addresses_in("status 0x0000000c") == [0x0C]
