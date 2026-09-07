"""Shared host environment for launching the packaged Astra QEMU."""

import os


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
