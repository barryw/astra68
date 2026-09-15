#!/usr/bin/env python3
"""The production publisher owns current-source builds and its workspace."""

import os
from pathlib import Path
import subprocess
import tempfile


HERE = Path(__file__).parent


def write_executable(path: Path, source: str) -> None:
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def run_case(deploy_status: int, mutate_source: bool = False) -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        repository = root / "repository"
        script_dir = repository / "emu/qemu"
        script_dir.mkdir(parents=True)
        (repository / ".gitignore").write_text(
            "build/\n__pycache__/\n", encoding="utf-8")
        (repository / "source.c").write_text("current source\n")
        (repository / "sw/userspace").mkdir(parents=True)
        (repository / "sw/boot").mkdir(parents=True)
        workspace_parent = root / "workspaces"
        workspace_parent.mkdir()
        publisher = script_dir / "publish-de25-release.sh"
        publisher.write_text(
            (HERE / publisher.name).read_text(encoding="utf-8"),
            encoding="utf-8")
        publisher.chmod(0o755)
        write_executable(script_dir / "astra_image.py", """#!/usr/bin/env python3
from pathlib import Path
import sys
Path(sys.argv[2]).touch()
""")
        write_executable(script_dir / "build.sh", """#!/bin/sh
set -eu
touch "$FAKE_QEMU"
chmod +x "$FAKE_QEMU"
printf '%s\\n' "$FAKE_QEMU"
""")
        write_executable(script_dir / "create-de25-release.sh", """#!/bin/sh
set -eu
mkdir -p "$1/rom"
: > "$1/rom/astra_boot.bin"
test -s "$ASTRA_DE25_SOURCE_MANIFEST"
chmod -R a-w "$1"
""")
        write_executable(script_dir / "deploy-de25-release.sh", f"""#!/bin/sh
test -f "$1/rom/astra_boot.bin"
exit {deploy_status}
""")
        fake_bin = root / "bin"
        fake_bin.mkdir()
        write_executable(fake_bin / "make", """#!/bin/sh
set -eu
printf '%s\\n' "$*" >> "$FAKE_MAKE_LOG"
if [ "${FAKE_MUTATE:-0}" = 1 ]; then
  printf 'changed\\n' >> "$PWD/repository/source.c"
fi
mkdir -p "$PWD/repository/tools/__pycache__"
printf 'generated\\n' >> "$PWD/repository/tools/__pycache__/cache.pyc"
case "$*" in
  *sw/boot*clean*) rm -rf "$PWD/repository/sw/boot/build" ;;
  *sw/boot*build/astra_boot.bin*) mkdir -p "$PWD/repository/sw/boot/build"; touch "$PWD/repository/sw/boot/build/astra_boot.bin" ;;
esac
""")
        environment = os.environ.copy()
        environment["TMPDIR"] = str(workspace_parent)
        environment["PATH"] = str(fake_bin) + os.pathsep + environment["PATH"]
        environment["DE25_SYSROOT"] = str(root / "sysroot")
        environment["FAKE_QEMU"] = str(root / "qemu-system-m68k")
        environment["FAKE_MAKE_LOG"] = str(root / "make.log")
        environment["FAKE_MUTATE"] = "1" if mutate_source else "0"
        result = subprocess.run([str(publisher)], env=environment,
                                check=False, cwd=root)
        assert result.returncode == (1 if mutate_source else deploy_status)
        assert list(workspace_parent.iterdir()) == []
        assert "sw/boot build/astra_boot.bin" in (root / "make.log").read_text()


run_case(0)
run_case(23)
run_case(0, mutate_source=True)
print("DE25 release publisher cleanup: PASS")
