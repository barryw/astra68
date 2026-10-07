"""Shared host environment for launching the packaged Astra QEMU."""

import os
import subprocess

DEFAULT_MEMORY_BYTES = 512 * 1024 * 1024
DEFAULT_MEMORY = "%dM" % (DEFAULT_MEMORY_BYTES // (1024 * 1024))


def qemu_environment(qemu, environment=None, hostfs_root=None):
    environment = (os.environ if environment is None else environment).copy()
    private_lib = os.path.realpath(
        os.path.join(os.path.dirname(qemu), "..", "lib"))
    if os.path.isdir(private_lib):
        existing = environment.get("LD_LIBRARY_PATH")
        environment["LD_LIBRARY_PATH"] = (
            private_lib if not existing else "%s:%s" % (private_lib,
                                                         existing))
    if hostfs_root is not None:
        os.makedirs(hostfs_root, exist_ok=True)
        environment["ASTRA_HOSTFS_ROOT"] = hostfs_root
    return environment


ROOT_WRAPPER = "/usr/local/sbin/astra-qemu-root"
ROOT_WRAPPER_SOURCE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools",
    "astra-qemu-root")


def root_provider(work, size_mib=256):
    """A fresh ext4 provider image and its empty mount point in `work`.

    `work` must be a caller-owned 0700 directory directly under /tmp (what
    tempfile.mkdtemp(dir="/tmp") makes). Returns (launcher prefix, mount
    point, image): the prefix runs QEMU as root through astra-qemu-root,
    the board's posture (FS11). Explicit features keep the image mountable
    by the board's e2fsprogs 1.46 (.superpowers/sdd/fs-linux-audit.md G15).
    """
    with open(ROOT_WRAPPER, "rb") as installed, \
            open(ROOT_WRAPPER_SOURCE, "rb") as source:
        if installed.read() != source.read():
            raise RuntimeError("%s is stale; reinstall tools/astra-qemu-root"
                               % ROOT_WRAPPER)
    image = os.path.join(work, "provider.img")
    mount = os.path.join(work, "provider")
    os.mkdir(mount)
    with open(image, "wb") as handle:
        handle.truncate(size_mib * 1024 * 1024)
    subprocess.run(["/usr/sbin/mkfs.ext4", "-q", "-F", "-O", "^orphan_file",
                    "-E", "root_owner=%d:%d" % (os.getuid(), os.getgid()),
                    image], check=True)
    return (["sudo", "-n", ROOT_WRAPPER, "--image", image, "--mount", mount,
             "--"], mount, image)
