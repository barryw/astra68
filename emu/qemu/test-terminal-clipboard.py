#!/usr/bin/env python3
"""Exercise Terminal selection and the system clipboard through real input."""

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import sys
import tempfile


HERE = Path(__file__).parent
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", HERE / "test-terminal.py")
terminal_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal_gate)

COMMAND = "print CLIPBOARD-PASTE-PASS"
# Terminal's default standard-window content origin is (182, 120). Its shared
# TextSurface begins at (10, 8), with 8x20 cells. The points below select row
# zero's exact half-open [0, len(COMMAND)) span.
SELECTION_START = (192, 136)
SELECTION_END = (192 + len(COMMAND) * 8, 136)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-clipboard-gate-") as root:
        image = os.path.join(root, "card.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, root)
        try:
            if not terminal_gate.open_terminal(
                    machine, arguments.boot_deadline,
                    arguments.command_deadline):
                return 1
            before = machine.sequence()
            machine.qmp.type_line(
                "print -n $'\\e[2J\\e[H'; print -r -- '" + COMMAND + "'")
            if terminal_gate.wait_for_command(
                    machine, COMMAND, arguments.command_deadline, before,
                    exact=True) is None:
                print("FAIL: selection fixture was not rendered")
                return 1
            machine.qmp.point(*SELECTION_START)
            machine.qmp.button(True)
            machine.qmp.point(*SELECTION_END)
            machine.qmp.button(False)
            machine.qmp.chord("alt", "c")
            before = machine.sequence()
            machine.qmp.chord("alt", "v")
            machine.qmp.key("ret")
            if terminal_gate.wait_for_command(
                    machine, "CLIPBOARD-PASTE-PASS",
                    arguments.command_deadline, before, exact=True) is None:
                print("FAIL: copied text was not pasted and executed")
                for line in machine.said(before)[0][-40:]:
                    print("    |%s|" % line)
                return 1
            print("ASTRA TERMINAL CLIPBOARD PASS")
            return 0
        finally:
            machine.close()


if __name__ == "__main__":
    sys.exit(main())
