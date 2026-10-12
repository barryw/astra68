"""Shared host environment for launching the packaged Astra QEMU."""

import os
import shutil
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


def root_provider(work, size_mib=256, system=None):
    """A fresh ext4 provider image in `work`, `work`/`sound` pre-populated
    at mkfs time, its root owned by the caller.

    Returns (launcher prefix, image). The prefix runs QEMU through
    astra-qemu-root, which loop-mounts a root-owned copy of the image
    under its own /var/tmp directory, announces the mount point as its
    first stdout line (`ASTRA-QEMU-ROOT-MOUNT PATH`, which
    test-terminal.Machine reads), then runs QEMU as the caller, never as
    root (FS11), inside QEMU's own private mount namespace, and finally
    writes the copy back to `image` with the caller's credentials.
    `work`/`sound` are made here because the helper never creates
    anything inside the mounted filesystem. Explicit features keep the
    image mountable by the board's e2fsprogs 1.46
    (.superpowers/sdd/fs-linux-audit.md G15).
    """
    with open(ROOT_WRAPPER, "rb") as installed, \
            open(ROOT_WRAPPER_SOURCE, "rb") as source:
        if installed.read() != source.read():
            raise RuntimeError("%s is stale; reinstall tools/astra-qemu-root"
                               % ROOT_WRAPPER)
    seed = os.path.join(work, "seed")
    os.makedirs(os.path.join(seed, "work"))
    os.makedirs(os.path.join(seed, "sound"))
    if system is not None:
        startup = os.path.join(seed, "system", "startup")
        os.makedirs(startup)
        shutil.copyfile(system, os.path.join(startup, "system"))
    image = os.path.join(work, "provider.img")
    with open(image, "wb") as handle:
        handle.truncate(size_mib * 1024 * 1024)
    subprocess.run(["/usr/sbin/mkfs.ext4", "-q", "-F", "-O", "^orphan_file",
                    "-E", "root_owner=%d:%d" % (os.getuid(), os.getgid()),
                    "-d", seed, image], check=True)
    return ["sudo", "-n", ROOT_WRAPPER, "--image", image, "--"], image
