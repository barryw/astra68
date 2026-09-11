#!/usr/bin/env python3
"""The production publisher owns and removes its release workspace."""

import os
from pathlib import Path
import subprocess
import tempfile


HERE = Path(__file__).parent


def write_executable(path: Path, source: str) -> None:
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def run_case(deploy_status: int) -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        workspace_parent = root / "workspaces"
        workspace_parent.mkdir()
        publisher = root / "publish-de25-release.sh"
        publisher.write_text(
            (HERE / publisher.name).read_text(encoding="utf-8"),
            encoding="utf-8")
        publisher.chmod(0o755)
        write_executable(root / "create-de25-release.sh", """#!/bin/sh
set -eu
mkdir -p "$1/rom"
: > "$1/rom/astra_boot.bin"
chmod -R a-w "$1"
""")
        write_executable(root / "deploy-de25-release.sh", f"""#!/bin/sh
test -f "$1/rom/astra_boot.bin"
exit {deploy_status}
""")
        environment = os.environ.copy()
        environment["TMPDIR"] = str(workspace_parent)
        result = subprocess.run([str(publisher)], env=environment,
                                check=False)
        assert result.returncode == deploy_status
        assert list(workspace_parent.iterdir()) == []


run_case(0)
run_case(23)
print("DE25 release publisher cleanup: PASS")
