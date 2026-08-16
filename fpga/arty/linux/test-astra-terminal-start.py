#!/usr/bin/env python3

import os
import pathlib
import subprocess
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
START = HERE / "astra_terminal_start.sh"


def run(statuses):
    with tempfile.TemporaryDirectory(prefix="astra-start-") as directory:
        root = pathlib.Path(directory)
        count = root / "count"
        runner = root / "run"
        runner.write_text("""#!/bin/sh
count=$(cat "$ASTRA_TEST_COUNT" 2>/dev/null || echo 0)
count=$((count + 1))
echo "$count" >"$ASTRA_TEST_COUNT"
eval "status=\\${ASTRA_TEST_STATUS_$count:-0}"
exit "$status"
""", encoding="ascii")
        runner.chmod(0o755)
        environment = os.environ.copy()
        environment.update({
            "ASTRA_ROOT": directory,
            "ASTRA_DATA_PATH": "/",
            "ASTRA_RUN_ARTY": str(runner),
            "ASTRA_RESTART_DELAY": "0",
            "ASTRA_TEST_COUNT": str(count),
        })
        for index, status in enumerate(statuses, 1):
            environment[f"ASTRA_TEST_STATUS_{index}"] = str(status)
        result = subprocess.run(["sh", str(START)], env=environment,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        return result.returncode, int(count.read_text(encoding="ascii"))


def main():
    assert run([139, 135, 0]) == (0, 3)
    assert run([1, 0]) == (1, 1)
    assert run([139, 139, 139, 139]) == (139, 4)
    assert "astra-chip-reset" not in START.read_text(encoding="ascii")
    print("Astra runtime supervisor tests passed")


if __name__ == "__main__":
    main()
