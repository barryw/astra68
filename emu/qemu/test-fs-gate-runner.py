#!/usr/bin/env python3
"""Gate failures remain failures; failed publication cannot start its consumer."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

SCRIPT = Path(__file__).with_name("fs-gates.sh").resolve()
with tempfile.TemporaryDirectory(prefix="astra-fs-gate-runner-") as temporary:
    root = Path(temporary)
    binaries = root / "bin"
    binaries.mkdir()
    recorder = binaries / "python3"
    recorder.write_text('''#!/usr/bin/python3
import json, os, sys
args = sys.argv[1:]
if args[0] == "-u": args = args[1:]
with open(os.environ["GATE_CALLS"], "a") as stream:
    stream.write(json.dumps({"args": args, "uid": os.environ.get("ASTRA_TEST_QEMU_UID"),
        "gid": os.environ.get("ASTRA_TEST_QEMU_GID"), "provider": os.environ.get("ASTRA_TEST_STORAGE_PROVIDER")}) + "\\n")
raise SystemExit(19 if os.path.basename(args[0]) in os.environ.get("GATE_FAILURES", "").split(",") else 0)
''')
    recorder.chmod(0o755)
    for name, body in {
        "id": 'if [ "$1" = "-u" ]; then echo 1000; else echo 1000; fi',
        "make": "exit 0",
        "bash": "exit 0",
        "sudo": 'test "$1" = "-n"; shift; exec "$@"',
    }.items():
        path = binaries / name
        path.write_text("#!/bin/sh\nset -eu\n" + body + "\n")
        path.chmod(0o755)
    inputs = []
    for name in ("qemu", "rom", "image", "provider", "client"):
        path = root / name
        path.write_text("fixture\n")
        path.chmod(0o755)
        inputs.append(str(path))
    calls = root / "calls"
    environment = dict(os.environ, PATH=str(binaries) + ":" + os.environ["PATH"], GATE_CALLS=str(calls))
    for name in ("ASTRA_TEST_QEMU_UID", "ASTRA_TEST_QEMU_GID", "ASTRA_TEST_STORAGE_PROVIDER"):
        environment.pop(name, None)

    def run(failures):
        calls.write_text("")
        environment["GATE_FAILURES"] = failures
        result = subprocess.run(["/bin/sh", str(SCRIPT)] + inputs, env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        evidence = re.search(r"Filesystem evidence: ([^;]+); failures: (\d+)", result.stdout)
        assert evidence is not None, result.stdout
        try:
            rows = [json.loads(line) for line in calls.read_text().splitlines()]
            for row in rows:
                if Path(row["args"][0]).name == "astra_image.py":
                    assert row["uid"] is None
                else:
                    assert row["uid"] == row["gid"] == "1000"
                    assert row["provider"] == inputs[3]
            return result, rows, int(evidence.group(2))
        finally:
            shutil.rmtree(evidence.group(1))

    passed, rows, failures = run("")
    assert passed.returncode == 0 and failures == 0, passed.stdout
    assert any(Path(row["args"][0]).name == "test-host-channel-userspace.py" for row in rows)
    failed, rows, failures = run("test-fs-semantics.py,astra_image.py")
    assert failed.returncode == 1 and failures == 2, failed.stdout
    assert any(Path(row["args"][0]).name == "test-filesystem-stress.py" for row in rows)
    assert not any(Path(row["args"][0]).name == "test-host-channel-userspace.py" for row in rows)
    for row in rows:
        if "--host-work" in row["args"]:
            path = row["args"][row["args"].index("--host-work") + 1]
            assert path.startswith("/tmp/astra-fs-gates."), path
print("Filesystem gate aggregation/dependency/observer contract: PASS")
