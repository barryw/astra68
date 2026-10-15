#!/usr/bin/env python3
"""Generate real FIT script and stage the full supplied DE25 kernel package.

Run on Beast with the complete build-production-storage.sh package as argv[1].
All installation paths are disposable directories; no board selector changes.
"""

import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

repository = Path(__file__).parents[2]
package = Path(sys.argv[1]).resolve()
source = repository / "fpga/de25/linux/kernel/install-production-storage.py"
spec = importlib.util.spec_from_file_location("kernel_install", source)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
recipe = (repository / "emu/qemu/create-de25-release.sh").read_text()
start = recipe.index("BOOT_WORKSPACE=$(mktemp")
end = recipe.index("# Astra's shared SoundFont", start)
with tempfile.TemporaryDirectory(prefix="astra-kernel-boot-gate-") as directory:
    fixture = Path(directory)
    environment = os.environ.copy()
    environment.update(REPOSITORY=str(repository), KERNEL_PACKAGE=str(package),
                       TMPDIR=str(fixture))
    output = subprocess.check_output(["sh"], input="set -eu\n" + recipe[start:end] +
        '\ntrap - EXIT HUP INT TERM\nprintf "ASTRA_BOOT_GATE %s\\n" "$BOOT_WORKSPACE"\n',
        text=True, env=environment)
    workspace = Path(next(line.removeprefix("ASTRA_BOOT_GATE ")
                          for line in output.splitlines() if line.startswith("ASTRA_BOOT_GATE ")))
    extracted = fixture / "extracted.cmd"
    timestamp = subprocess.check_output(["fdtget", "-t", "u", str(workspace / "boot.scr.uimg"),
                                         "/", "timestamp"], text=True)
    assert timestamp.strip() == "0", "FIT timestamp must be reproducible across runtime releases"
    subprocess.run(["dumpimage", "-T", "flat_dt", "-p", "0", "-o", str(extracted),
                    str(workspace / "boot.scr.uimg")], check=True)
    identity = hashlib.sha256((package / "SHA256SUMS").read_bytes()).hexdigest()
    original = (repository / "fpga/de25/boot.cmd").read_text()
    actual = extracted.read_text()
    for filename in ("Image", "socfpga_agilex5_de25_nano.dtb"):
        actual = actual.replace("} astra-kernels/" + identity + "/" + filename + ";",
                                "} " + filename + ";")
    assert actual == original, "generated script changed canonical fabric/boot policy"
    kernel = fixture / "kernel"
    kernel.mkdir()
    shutil.copytree(package, kernel / "package")
    for name in ("boot.identity", "boot.scr.uimg"):
        shutil.copyfile(workspace / name, kernel / name)
    root = fixture / "root"
    boot = fixture / "boot"
    root.mkdir()
    boot.mkdir()
    (boot / "boot.scr.uimg").write_bytes(b"existing selector untouched")
    state = root / "state.json"
    result = installer.stage(kernel, root, boot, state)
    assert result[0] == identity
    assert (boot / "boot.scr.uimg").read_bytes() == b"existing selector untouched"
    assert not state.exists()
    module_count = sum(1 for path in (root / "lib/modules" / result[1]).rglob("*.ko"))
    assert module_count > 1000, "incomplete production module tree"
    assert installer.stage(kernel, root, boot, state) == result
    print("DE25 real FIT/full kernel stage: PASS identity=%s modules=%d" % (identity, module_count))
