#!/usr/bin/env python3
"""Exercise complete kernel staging, boot commit and exact rollback."""

import hashlib
import importlib.util
from pathlib import Path
import tempfile

SOURCE = Path(__file__).parents[2] / "fpga/de25/linux/kernel/install-production-storage.py"
spec = importlib.util.spec_from_file_location("kernel_install", SOURCE)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def refused(operation):
    try:
        operation()
    except (RuntimeError, OSError):
        return
    raise AssertionError("unsafe kernel operation accepted")


def fixture(root, edition="1"):
    kernel = root / "kernel"
    package = kernel / "package"
    release = "6.12.11-astra-fixture" + edition
    content = {
        "boot/Image": b"new Image",
        "boot/dtbs/" + installer.DTB: b"new board DTB",
        "metadata/kernel.release": (release + "\n").encode(),
        "lib/modules/%s/modules.dep" % release: b"complete depmod output\n",
    }
    for name in ("kernel/drivers/scsi/sr_mod.ko", "kernel/drivers/cdrom/cdrom.ko",
                 "kernel/fs/isofs/isofs.ko", "kernel/fs/udf/udf.ko",
                 "kernel/fs/exfat/exfat.ko", "kernel/fs/ntfs3/ntfs3.ko",
                 "extra/astra/astra_graphics_arena.ko",
                 "extra/astra/astra_display_capture.ko", "kernel/other.ko"):
        content["lib/modules/%s/%s" % (release, name)] = name.encode()
    lines = []
    for name, payload in sorted(content.items()):
        path = package / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        lines.append(hashlib.sha256(payload).hexdigest() + "  " + name + "\n")
    (package / "SHA256SUMS").write_text("".join(lines))
    identity = installer.digest(package / "SHA256SUMS")
    (kernel / "boot.identity").write_text(identity + "\n")
    (kernel / "boot.scr.uimg").write_bytes(b"new exact boot script")
    boot = root / "boot"
    boot.mkdir()
    (boot / "boot.scr.uimg").write_bytes(b"original boot script")
    (boot / "Image").write_bytes(b"original Image")
    (boot / "socfpga_agilex5_de25_nano.dtb").write_bytes(b"original DTB")
    modules = root / "lib/modules/vendor-release"
    modules.mkdir(parents=True)
    (modules / "original.ko").write_bytes(b"original module")
    return kernel, package, release, identity, boot


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    kernel, package, release, identity, boot = fixture(root)
    state = root / "state/boot.json"
    old = installer.digest(boot / "boot.scr.uimg")
    result = installer.stage(kernel, root, boot)
    assert result[:2] == (identity, release)
    assert installer.digest(boot / "boot.scr.uimg") == old
    assert not state.exists()
    assert (boot / "Image").read_bytes() == b"original Image"
    assert (root / "lib/modules/vendor-release/original.ko").read_bytes() == b"original module"
    assert installer.stage(kernel, root, boot) == result
    refused(lambda: installer.verify_running(kernel, root, boot, release))
    installer.arm(kernel, root, boot, state)
    assert installer.verify_running(kernel, root, boot, release) == (identity, release)
    assert state.is_file()
    assert (boot / "boot.scr.uimg").read_bytes() == b"new exact boot script"
    installer.arm(kernel, root, boot, state)  # Retry retains original rollback.
    installer.rollback(boot, state)
    assert installer.digest(boot / "boot.scr.uimg") == old
    assert not state.exists()
    installer.arm(kernel, root, boot, state)
    (boot / "boot.scr.uimg").write_bytes(b"outside modification")
    refused(lambda: installer.rollback(boot, state))
    refused(lambda: installer.verify_running(kernel, root, boot, release))
    assert state.exists()

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    kernel, package, release, identity, boot = fixture(root)
    (package / "lib/modules" / release / "kernel/fs/ntfs3/ntfs3.ko").unlink()
    refused(lambda: installer.stage(kernel, root, boot))
    assert not (root / "lib/modules" / release).exists()

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    kernel, package, release, identity, boot = fixture(root)
    module = package / "lib/modules" / release / "kernel/other.ko"
    module.write_bytes(b"tampered module")
    refused(lambda: installer.stage(kernel, root, boot))
    assert not (root / "lib/modules" / release).exists()
    assert (boot / "boot.scr.uimg").read_bytes() == b"original boot script"

# After acceptance keep current/previous boot and module generations only.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    first = root / "first"
    first.mkdir()
    kernel, _package, release, identity, boot = fixture(first)
    state = root / "state.json"
    installer.arm(kernel, root, boot, state)
    installer.confirm(kernel, root, boot, state, release)
    assert not state.exists() and installer.previous_state(state).exists()
    identities = [identity]
    releases = [release]
    for edition in ("2", "3"):
        next_root = root / ("fixture-" + edition)
        next_root.mkdir()
        next_kernel, _package, next_release, next_identity, _unused_boot = fixture(next_root, edition)
        # Distinct script makes the selected Image/DTB generation observable.
        (next_kernel / "boot.scr.uimg").write_bytes(("script " + edition).encode())
        installer.arm(next_kernel, root, boot, state)
        refused(lambda: installer.confirm(next_kernel, root, boot, state, "wrong-release"))
        installer.confirm(next_kernel, root, boot, state, next_release)
        identities.append(next_identity)
        releases.append(next_release)
    assert not (boot / "astra-kernels" / identities[0]).exists()
    assert not (root / "lib/modules" / releases[0]).exists()
    for index in (1, 2):
        assert (boot / "astra-kernels" / identities[index]).is_dir()
        assert (root / "lib/modules" / releases[index]).is_dir()
    installer.rollback(boot, state)  # Rollback still available after confirm.
    assert (boot / "boot.scr.uimg").read_bytes() == b"script 2"

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    kernel, package, release, identity, boot = fixture(root)
    installer.stage(kernel, root, boot)
    (root / "lib/modules" / release / "kernel/other.ko").write_bytes(b"installed tamper")
    refused(lambda: installer.arm(kernel, root, boot, root / "state.json"))
    assert (boot / "boot.scr.uimg").read_bytes() == b"original boot script"

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    kernel, package, release, identity, boot = fixture(root)
    (package / "unexpected").write_bytes(b"unmanifested")
    refused(lambda: installer.stage(kernel, root, boot))
    (package / "unexpected").unlink()
    target = package / "boot/Image"
    target.unlink()
    target.symlink_to(boot / "Image")
    refused(lambda: installer.stage(kernel, root, boot))

print("DE25 complete kernel install/arm/rollback: PASS")
