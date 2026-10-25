"""Shared environment and confined storage launch contract for Astra QEMU."""

import os
from pathlib import Path
import shlex
import subprocess

from storage_image import image_window

DEFAULT_MEMORY_BYTES = 512 * 1024 * 1024
DEFAULT_MEMORY = "%dM" % (DEFAULT_MEMORY_BYTES // (1024 * 1024))
STORAGE_PROVIDER = "/usr/local/libexec/astra-storage"
ROOT_WRAPPER = "/usr/local/sbin/astra-qemu-root"
ROOT_WRAPPER_SOURCE = Path(__file__).resolve().parents[2] / "tools/astra-qemu-root"
LAUNCHER_EXPORTS = "ASTRA-QEMU-ROOT-EXPORTS "


def qemu_test_credentials(credentials=None):
    """One explicit credential policy for trusted root observers."""
    if credentials is None:
        credentials = (os.geteuid(), os.getegid()) if os.geteuid() != 0 else (
            int(os.environ.get("ASTRA_TEST_QEMU_UID", "0")),
            int(os.environ.get("ASTRA_TEST_QEMU_GID", "0")))
    if min(credentials) <= 0:
        raise ValueError("root observer needs explicit non-root QEMU UID/GID")
    return credentials


def unprivileged_command(command, credentials=None):
    """Paused/device-only tests still may not execute QEMU as root."""
    if os.geteuid() != 0:
        return list(command)
    credentials = qemu_test_credentials(credentials)
    return ["setpriv", "--reuid", str(credentials[0]), "--regid", str(credentials[1]),
            "--clear-groups", "--no-new-privs", "--bounding-set=-all",
            "--inh-caps=-all", "--ambient-caps=-all"] + list(command)


def qemu_environment(qemu, environment=None):
    environment = (os.environ if environment is None else environment).copy()
    if "ASTRA_HOSTFS_ROOT" in environment:
        raise ValueError("ASTRA_HOSTFS_ROOT was replaced by the confined storage provider")
    private_lib = os.path.realpath(os.path.join(os.path.dirname(qemu), "..", "lib"))
    if os.path.isdir(private_lib):
        existing = environment.get("LD_LIBRARY_PATH")
        environment["LD_LIBRARY_PATH"] = private_lib if not existing else "%s:%s" % (private_lib, existing)
    return environment


def provider_command(provider, image, exports, uid, gid, discovery="none"):
    """Trusted root fixture/service configuration, never an untrusted sudo API."""
    if uid <= 0 or gid <= 0 or discovery not in ("none", "udev"):
        raise ValueError("explicit non-root QEMU UID/GID and discovery policy required")
    offset, length, uuid = image_window(image)
    exports = Path(exports).resolve()
    for name in ("work", "sound"):
        (exports / name).mkdir(parents=True, exist_ok=True)
    return [str(Path(provider).resolve()), "--uid", str(uid), "--gid", str(gid),
            "--system", str(Path(image).resolve()), str(offset), str(length), uuid,
            "--work", str(exports / "work"), "--sound", str(exports / "sound"),
            "--limits", "8388608", "8388608", "4", "10000", "4",
            "100663296", "8192", "67108864", "4096", "33554432", "4096",
            "--discovery", discovery, "--seed-sound", "none", "--"]


def storage_fixture(image, directory, exports=None, credentials=None):
    """Root observer launches one provider and non-root QEMU for desktop gates.

    The root observer reads preserved root-owned logs without changing modes.
    QEMU receives only authenticated provider endpoints and zero privilege.
    A regular caller uses the separately installed hostile-caller wrapper.
    """
    if os.geteuid() != 0:
        if exports is not None:
            raise ValueError("external test exports require a root observer")
        if ROOT_WRAPPER_SOURCE.read_bytes() != Path(ROOT_WRAPPER).read_bytes():
            raise RuntimeError("%s is stale; reinstall tools/astra-qemu-root" % ROOT_WRAPPER)
        return ["sudo", "-n", ROOT_WRAPPER, "--image", str(Path(image).resolve()), "--"], None
    credentials = qemu_test_credentials(credentials)
    directory = Path(directory).resolve()
    exports = Path(exports).resolve() if exports is not None else directory / "exports"
    command = provider_command(os.environ.get("ASTRA_TEST_STORAGE_PROVIDER", STORAGE_PROVIDER),
                               image, exports, *credentials)
    fixture = directory / "storage-fixture.sh"
    fixture.write_text("#!/bin/sh\nset -eu\nprintf '%s\\n' " +
                       shlex.quote(LAUNCHER_EXPORTS + str(exports)) +
                       "\nexec " + shlex.join(command) + ' "$@"\n')
    return ["sh", str(fixture)], credentials


def make_ext4_image(work, size_mib=256):
    """Disposable standalone filesystem for probe tests; no launch authority."""
    image = Path(work) / "provider.img"
    with image.open("wb") as handle:
        handle.truncate(size_mib * 1024 * 1024)
    subprocess.run(["/usr/sbin/mkfs.ext4", "-q", "-F", "-O", "^orphan_file", str(image)], check=True)
    return str(image)
