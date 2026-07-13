#!/usr/bin/env python3
"""Architecturally comparable 68000 CCR bits for the current Harte subset."""

X = 0x10
N = 0x08
Z = 0x04
V = 0x02
C = 0x01
ALL = X | N | Z | V | C


def mask_for_opcode(opcode: int) -> int:
    # Every currently admitted opcode either defines each CCR bit or explicitly
    # preserves it. Forms with undefined flags (notably BCD N/V) are filtered.
    del opcode
    return ALL
