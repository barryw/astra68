#!/usr/bin/env python3
"""Pin the DE25 release to its AArch64 runtime dependencies."""

from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile


script = Path(__file__).with_name("create-de25-release.sh").read_text()
for required in (
    "ASTRA_DE25_QEMU",
    "ASTRA_DE25_ROM",
    "ASTRA_DE25_STORAGE",
    "ASTRA_DE25_QEMU_LIBDIR",
    "ASTRA_DE25_SOURCE_MANIFEST",
    "ASTRA_DE25_SYSROOT",
    "ASTRA_DE25_KERNEL_PACKAGE",
    "build/de25-graphics/linux/astra-terminal-display",
    'make -B -j "$JOBS" -C "$REPOSITORY/fpga/arty/linux"',
    'make -B -j "$JOBS" -C "$REPOSITORY/fpga/de25/linux"',
    "bin/astra-remote-desktop=",
    "bin/astra-audio-host=",
    "bin/astra-font-worker=",
    "bin/astra-codec-worker=",
    "font-worker-target",
    "bin/fs-baseline.py=",
    "systemd/astra-remote-desktop.service=",
    "systemd/astra-audio-host.service=",
    "systemd/astra.service=",
    "getconf _NPROCESSORS_ONLN",
    "qemu/lib/libpixman-1.so.0=",
    "qemu/lib/libpcre.so.3=",
    "qemu/lib/libglib-2.0.so.0=",
    "source/SOURCE_SHA256SUMS=",
    "storage-provider-target",
    "bin/astra-storage=",
    "bin/astra-mkfs.fat=",
    "bin/astra-mkfs.exfat=",
    "bin/storage_image.py=",
    "bin/astra-system-image-migrate.py=",
    "kernel/package/$relative=",
    "kernel/package/SHA256SUMS=",
    "bin/astra-kernel-install.py=",
    "kernel/boot.scr.uimg=",
    "kernel/boot.identity=",
    "astra-kernels/",
    "udev/99-astra-qemu.rules=",
    'python3 "$RELEASE_TOOL" create',
):
    assert required in script, required
assert "ASTRA_DE25_TERMINAL_DISPLAY" not in script
assert '"kernel/astra_graphics_arena.ko=' not in script

udev_rules = (Path(__file__).parents[2] /
              "fpga/de25/99-astra-qemu.rules").read_text()
assert 'ENV{ID_INPUT_JOYSTICK}=="1", GROUP="astra", MODE="0660"' in udev_rules

graphics_makefile = (Path(__file__).parents[2] /
                     "fpga/arty/linux/Makefile").read_text()
assert "$(NDK_INCLUDE)/astra/theme.h" in graphics_makefile

remote_unit = (Path(__file__).parents[2] /
               "fpga/de25/astra-remote-desktop.service").read_text()
assert "Before=astra.service" in remote_unit
assert "WantedBy=astra.service" in remote_unit
assert "RuntimeDirectory=astra" in remote_unit
assert "RuntimeDirectoryPreserve=yes" in remote_unit
assert "After=astra.service" not in remote_unit
assert "Requires=astra.service" not in remote_unit

# Execute the actual constructor with fresh build outputs. The fake make
# follows the Linux Makefile's default OUT, so omitting the explicit provider
# output directory fails instead of silently borrowing a stale binary.
repository = Path(__file__).resolve().parents[2]
for stale_provider in (False, True):
    with tempfile.TemporaryDirectory(prefix="astra-release-output-") as directory:
        fixture = Path(directory)
        tree = fixture / "repository"
        script_dir = tree / "emu/qemu"
        script_dir.mkdir(parents=True)
        constructor = script_dir / "create-de25-release.sh"
        constructor.write_text(script)
        constructor.chmod(0o755)
        for name in ("fpga/de25/boot.cmd", "fpga/de25/boot.its", "tools/astra_release.py"):
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repository / name, target)
        for name in (
            "fpga/de25/astra.service", "fpga/de25/astra-audio-host.service",
            "fpga/de25/astra-remote-desktop.service", "fpga/de25/99-astra-qemu.rules",
            "fpga/de25/linux/kernel/install-production-storage.py",
            "emu/qemu/storage_image.py", "emu/qemu/migrate-system-image.py",
            "emu/qemu/astra-input-hotplug.py",
            "emu/qemu/run-arty.sh", "emu/qemu/runtime-supervisor.py",
            "emu/qemu/astra-top.py", "emu/qemu/de25-ab.sh", "emu/qemu/de25-rfb.py",
            "emu/qemu/fs-baseline.py",
            "sw/userspace/services/media/build/soundfonts/default",
            "sw/userspace/services/media/build/soundfonts/fixture.sf2",
        ):
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("fixture " + name + "\n")
        (tree / "fpga/arty/linux").mkdir(parents=True)
        (tree / "fpga/de25/linux").mkdir(exist_ok=True)
        formatter_build = tree / "third_party/storage-format/build.sh"
        formatter_build.parent.mkdir(parents=True)
        formatter_build.write_text('''#!/bin/sh
set -eu
test "$1" = de25
directory=$(dirname "$0")/../../build/storage-format/de25
mkdir -p "$directory"
printf 'fresh fat formatter\\n' > "$directory/astra-mkfs.fat"
printf 'fresh exfat formatter\\n' > "$directory/astra-mkfs.exfat"
chmod 755 "$directory/astra-mkfs.fat" "$directory/astra-mkfs.exfat"
''')
        fake_bin = fixture / "bin"
        fake_bin.mkdir()
        make = fake_bin / "make"
        make.write_text('''#!/usr/bin/env python3
import os, pathlib, sys
args = sys.argv[1:]
tree = pathlib.Path(os.environ["RELEASE_OUTPUT_FIXTURE"])
directory = pathlib.Path(args[args.index("-C") + 1])
out = next((pathlib.Path(arg[4:]) for arg in args if arg.startswith("OUT=")),
           tree / "build/de25-capture")
if "storage-provider-target" in args:
    products = [out / "astra-storage-aarch64"]
elif "font-worker-target" in args:
    products = [out / "astra-font-worker-aarch64"]
elif "codec-worker-target" in args:
    products = [out / "astra-codec-worker-aarch64"]
elif "remote-desktop" in args:
    products = [out / "astra-remote-desktop"]
else:
    products = [(directory / arg).resolve() for arg in args
                if arg.startswith("../../../build/")]
assert products, args
for product in products:
    product.parent.mkdir(parents=True, exist_ok=True)
    product.write_bytes(("fresh " + product.name + "\\n").encode())
    product.chmod(0o755)
''')
        make.chmod(0o755)
        sysroot = fixture / "sysroot"
        libraries = sysroot / "usr/lib/aarch64-linux-gnu"
        libraries.mkdir(parents=True)
        for name in ("libpixman-1.so.0", "libpcre.so.3", "libglib-2.0.so.0",
                     "libblkid.so.1", "libmount.so.1", "libudev.so.1",
                     "libselinux.so.1", "libpcre2-8.so.0"):
            (libraries / name).write_text("fixture " + name)
        font_libraries = sysroot / "lib/aarch64-linux-gnu"
        font_libraries.mkdir(parents=True)
        for name in ("libfreetype.so.6", "libpng16.so.16",
                     "libbrotlidec.so.1", "libbrotlicommon.so.1", "libz.so.1"):
            (font_libraries / name).write_text("fixture " + name)
        package = fixture / "kernel-package"
        kernel_release = "6.12.11-astra-fixture"
        package_files = {
            "metadata/kernel.release": kernel_release + "\n",
            "boot/Image": "fixture Image",
            "boot/dtbs/intel/socfpga_agilex5_de25_nano.dtb": "fixture DTB",
            "lib/modules/" + kernel_release + "/kernel/drivers/scsi/sr_mod.ko": "fixture sr",
            "lib/modules/" + kernel_release + "/extra/astra/astra_graphics_arena.ko": "fixture arena",
        }
        for name, value in package_files.items():
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(value)
        (package / "SHA256SUMS").write_text("".join(
            hashlib.sha256((package / name).read_bytes()).hexdigest() + "  " + name + "\n"
            for name in sorted(package_files)))
        for name in ("qemu", "rom", "storage", "source-manifest"):
            (fixture / name).write_text("fixture " + name)
        provider = tree / "build/de25-linux/astra-storage-aarch64"
        if stale_provider:
            provider.parent.mkdir(parents=True)
            provider.write_bytes(b"stale provider from another build\n")
        output = fixture / "release"
        environment = dict(os.environ,
            PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
            RELEASE_OUTPUT_FIXTURE=str(tree),
            ASTRA_DE25_QEMU=str(fixture / "qemu"), ASTRA_DE25_ROM=str(fixture / "rom"),
            ASTRA_DE25_STORAGE=str(fixture / "storage"), ASTRA_DE25_QEMU_LIBDIR=str(libraries),
            ASTRA_DE25_SOURCE_MANIFEST=str(fixture / "source-manifest"),
            ASTRA_DE25_SYSROOT=str(sysroot), ASTRA_DE25_KERNEL_PACKAGE=str(package))
        try:
            result = subprocess.run([str(constructor), str(output)], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert result.returncode == 0, result.stdout
            assert (output / "bin/astra-storage").read_bytes() == b"fresh astra-storage-aarch64\n"
            assert (output / "bin/astra-mkfs.fat").read_bytes() == b"fresh fat formatter\n"
            assert (output / "bin/astra-mkfs.exfat").read_bytes() == b"fresh exfat formatter\n"
            selector = output / "bin/storage_image.py"
            assert selector.read_text() == "fixture emu/qemu/storage_image.py\n"
            assert selector.stat().st_mode & 0o111 == 0
            assert (output / "bin/astra-system-image-migrate.py").read_text() == \
                "fixture emu/qemu/migrate-system-image.py\n"
            assert provider.read_bytes() == (output / "bin/astra-storage").read_bytes()
            assert (output / "bin/astra-font-worker").read_bytes() == \
                b"fresh astra-font-worker-aarch64\n"
            assert (output / "bin/astra-codec-worker").read_bytes() == \
                b"fresh astra-codec-worker-aarch64\n"
            subprocess.run(["python3", str(tree / "tools/astra_release.py"),
                            "verify", str(output)], check=True, stdout=subprocess.DEVNULL)
        finally:
            # The real release constructor seals its result read-only.
            for target in [output] + list(output.rglob("*")):
                if target.exists() and not target.is_symlink():
                    target.chmod(target.stat().st_mode | 0o200)
print("DE25 fresh provider output and stale-artifact replacement: PASS")

print("DE25 release creation contract: PASS")
