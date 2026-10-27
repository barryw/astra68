#!/usr/bin/env python3
"""One command: exact source sync, focused Beast build, DE25 guest install."""

from pathlib import Path
import shlex
import subprocess
import sys


REPOSITORY = Path(__file__).resolve().parent.parent


def main():
    subprocess.run(["rtk", "rsync", "-az", "--checksum", "--no-times",
                    "--delete", "--exclude", ".git", "--exclude", "build",
                    "--exclude", "*/build", str(REPOSITORY) + "/",
                    "beast:/home/barry/astra68/"], check=True)
    command = "cd /home/barry/astra68 && python3 emu/qemu/dev-deploy-de25.py " + \
        " ".join(shlex.quote(argument) for argument in sys.argv[1:])
    subprocess.run(["rtk", "proxy", "ssh", "beast", command], check=True)


if __name__ == "__main__":
    main()
