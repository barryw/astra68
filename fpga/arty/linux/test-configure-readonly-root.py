#!/usr/bin/env python3

import pathlib
import subprocess
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
CONFIGURE = (HERE / "rootfs-overlay/usr/sbin/"
             "astra-configure-readonly-root")
LINKS = {
    "tmp": "/var/tmp",
    "var/log": "/var/volatile/log",
    "var/lock": "/run/lock",
    "var/run": "/run",
    "var/tmp": "/var/volatile/tmp",
    "etc/resolv.conf": "/var/run/resolv.conf",
}


def make_root(directory, populate):
    root = pathlib.Path(directory)
    (root / "etc/default").mkdir(parents=True)
    (root / "etc/init.d").mkdir(parents=True)
    (root / "etc/default/rcS").write_text(
        "ROOTFS_READ_ONLY=no\nVOLATILE_ENABLE_CACHE=yes\n", encoding="ascii")
    script = root / "etc/init.d/populate-volatile.sh"
    script.write_text(populate, encoding="ascii")
    script.chmod(0o755)
    return root


def main():
    populate = """#!/bin/sh
root=${0%/etc/init.d/populate-volatile.sh}
mkdir -p "$root/var/volatile/log" "$root/var/volatile/tmp" "$root/run/lock"
for path in tmp var/log var/lock var/run var/tmp etc/resolv.conf; do
    rm -rf "$root/$path"
done
ln -s /var/tmp "$root/tmp"
ln -s /var/volatile/log "$root/var/log"
ln -s /run/lock "$root/var/lock"
ln -s /run "$root/var/run"
ln -s /var/volatile/tmp "$root/var/tmp"
ln -s /var/run/resolv.conf "$root/etc/resolv.conf"
"""
    with tempfile.TemporaryDirectory(prefix="astra-readonly-root-") as directory:
        root = make_root(directory, populate)
        subprocess.run(["sh", str(CONFIGURE), str(root)], check=True)
        settings = (root / "etc/default/rcS").read_text(encoding="ascii")
        assert "ROOTFS_READ_ONLY=yes" in settings
        for path, target in LINKS.items():
            assert (root / path).is_symlink()
            assert (root / path).readlink() == pathlib.Path(target)

    with tempfile.TemporaryDirectory(prefix="astra-readonly-root-bad-") as directory:
        root = make_root(directory, "#!/bin/sh\nexit 0\n")
        result = subprocess.run(["sh", str(CONFIGURE), str(root)])
        assert result.returncode != 0
    print("Astra read-only root provisioning tests passed")


if __name__ == "__main__":
    main()
