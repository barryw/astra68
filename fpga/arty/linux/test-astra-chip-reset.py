#!/usr/bin/env python3

import os
import pathlib
import subprocess
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
RESET = HERE / "astra_chip_reset.sh"


def main():
    with tempfile.TemporaryDirectory(prefix="astra-chip-reset-") as directory:
        root = pathlib.Path(directory)
        state = root / "state"
        log = root / "log"
        ethernet = root / "eth0"
        ethernet.mkdir()
        state.write_text("0x00000000\n", encoding="ascii")
        ip = root / "ip"
        ip.write_text("""#!/bin/sh
echo "ip $@" >>"$ASTRA_TEST_DEVMEM_LOG"
""", encoding="ascii")
        ip.chmod(0o755)
        devmem = root / "devmem"
        devmem.write_text("""#!/bin/sh
echo "$@" >>"$ASTRA_TEST_DEVMEM_LOG"
if [ "$#" -eq 3 ]; then
    [ "$1" != 0xF8000240 ] || echo "$3" >"$ASTRA_TEST_DEVMEM_STATE"
    exit 0
fi
case "$1" in
    0xF8000240) cat "$ASTRA_TEST_DEVMEM_STATE" ;;
    0x43C00000) echo 0x41535452 ;;
    0x43C06000) echo 0x41554430 ;;
    0x43C07000) echo 0x504E4C30 ;;
    *) echo 0x00000000 ;;
esac
""", encoding="ascii")
        devmem.chmod(0o755)
        environment = os.environ.copy()
        environment.update({
            "ASTRA_DEVMEM": str(devmem),
            "ASTRA_IP": str(ip),
            "ASTRA_ETHERNET_PATH": str(ethernet),
            "ASTRA_TEST_DEVMEM_LOG": str(log),
            "ASTRA_TEST_DEVMEM_STATE": str(state),
        })
        subprocess.run(["sh", str(RESET)], env=environment, check=True)
        lines = log.read_text(encoding="ascii").splitlines()
        assert lines[0] == "ip link set eth0 up"
        writes = [line for line in lines
                  if len(line.split()) == 3]
        assert writes == [
            "0xF8000008 32 0x0000DF0D",
            "0xF8000240 32 2",
            "0xF8000240 32 0",
        ]
        assert state.read_text(encoding="ascii").strip() == "0"
    print("Astra chip reset tests passed")


if __name__ == "__main__":
    main()
