#!/usr/bin/env python3
"""Run the Linux mount seam in its own private namespace on disposable images."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--provider", action="store_true")
    parser.add_argument("--exfat-tool")
    parser.add_argument("--nfs-server")
    parser.add_argument("--smb-server")
    parser.add_argument("--smb-auth-server")
    parser.add_argument("--formatter-fat")
    parser.add_argument("--formatter-exfat")
    parser.add_argument("--owner-uid", type=int, default=os.getuid())
    parser.add_argument("--owner-gid", type=int, default=os.getgid())
    args = parser.parse_args()
    if args.owner_uid <= 0 or args.owner_gid <= 0:
        parser.error("mount owner must be non-root; set --owner-uid and --owner-gid")
    if args.exfat_tool and not args.provider:
        parser.error("--exfat-tool requires --provider")
    if args.nfs_server and not args.provider:
        parser.error("--nfs-server requires --provider")
    if args.smb_server and not args.provider:
        parser.error("--smb-server requires --provider")
    if args.smb_auth_server and not args.provider:
        parser.error("--smb-auth-server requires --provider")
    if bool(args.formatter_fat) != bool(args.formatter_exfat) or (
            args.formatter_fat and not args.provider):
        parser.error("both formatters require --provider")
    binary = str(Path(args.binary).resolve())
    with tempfile.TemporaryDirectory(prefix="astra-storage-mount-") as work:
        seed = Path(work) / "seed"
        (seed / "Folder").mkdir(parents=True)
        (seed / "Folder" / "café.txt").write_text("Astra ISO test\n")
        images = {}
        for name, format_id, extra in (
                ("rock-ridge.iso", "4", ["-R", "-J"]),
                ("joliet.iso", "4", ["-J"]),
                ("disc.udf", "5", ["-R", "-J", "-udf"])):
            if args.provider and name == "joliet.iso":
                continue
            image = str(Path(work) / name)
            subprocess.run([
                "genisoimage", "-quiet", "-input-charset", "utf-8",
                "-V", "ASTRA_DISC", "-o", image
            ] + extra + [str(seed)], check=True)
            images[name] = image
            if not args.provider:
                subprocess.run(["sudo", "-n", binary, image, format_id,
                                str(args.owner_uid), str(args.owner_gid)], check=True)
        for bits, size, format_id in ((12, 4, "7"), (16, 32, "8"), (32, 64, "2")):
            image = str(Path(work) / f"fat{bits}.img")
            with open(image, "wb") as handle:
                handle.truncate(size * 1024 * 1024)
            subprocess.run(["/usr/sbin/mkfs.vfat", "-F", str(bits), "-n",
                            f"ASTRA_FAT{bits}", image],
                           check=True, stdout=subprocess.DEVNULL)
            images[f"fat{bits}.img"] = image
            if not args.provider and bits != 32:
                subprocess.run(["sudo", "-n", binary, image, format_id,
                                str(args.owner_uid), str(args.owner_gid)], check=True)
        ntfs = str(Path(work) / "ntfs.img")
        with open(ntfs, "wb") as handle:
            handle.truncate(64 * 1024 * 1024)
        subprocess.run(["/usr/sbin/mkfs.ntfs", "-F", "-Q", "-L",
                        "ASTRA_NTFS", ntfs], check=True,
                       stdout=subprocess.DEVNULL)
        images["ntfs.img"] = ntfs
        if args.provider:
            if args.formatter_fat:
                staged = Path(work) / "test-storage-provider"
                shutil.copy2(binary, staged)
                subprocess.run(["sudo", "-n", "install", "-o", "root", "-g", "root",
                                "-m", "0755", args.formatter_fat,
                                str(Path(work) / "astra-mkfs.fat")], check=True)
                subprocess.run(["sudo", "-n", "install", "-o", "root", "-g", "root",
                                "-m", "0755", args.formatter_exfat,
                                str(Path(work) / "astra-mkfs.exfat")], check=True)
                binary = str(staged)
            if args.exfat_tool:
                exfat = str(Path(work) / "exfat.img")
                with open(exfat, "wb") as handle:
                    handle.truncate(64 * 1024 * 1024)
                subprocess.run([args.exfat_tool, "-q", "-L", "ASTRA_EXFAT", exfat],
                               check=True)
                images["exfat.img"] = exfat
            sudo = ["sudo", "-n"]
            if args.nfs_server:
                sudo.append("ASTRA_TEST_NFS_SERVER=" + args.nfs_server)
            if args.smb_server:
                sudo.append("ASTRA_TEST_SMB_SERVER=" + args.smb_server)
            if args.smb_auth_server:
                sudo.append("ASTRA_TEST_SMB_AUTH_SERVER=" + args.smb_auth_server)
            subprocess.run(sudo + [binary, images["rock-ridge.iso"],
                            str(args.owner_uid), str(args.owner_gid),
                            images["fat12.img"], images["fat16.img"],
                            images["fat32.img"], images["ntfs.img"],
                            images["disc.udf"]] +
                           ([images["exfat.img"]] if args.exfat_tool else []) +
                           (["--format-gate"] if args.formatter_fat else []), check=True)
            if args.formatter_fat:
                checker = str(Path(args.formatter_fat).with_name("astra-fsck.fat"))
                subprocess.run([checker, "-n", images["fat32.img"]],
                               check=True, stdout=subprocess.DEVNULL)
        else:
            subprocess.run(["sudo", "-n", binary, ntfs, "9",
                            str(args.owner_uid), str(args.owner_gid)], check=True)


if __name__ == "__main__":
    main()
