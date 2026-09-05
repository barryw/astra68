#!/usr/bin/env python3
"""Refuse DE25 builds on an unqualified host, board, or vendor baseline."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


HERE = Path(__file__).resolve().parent


def fail(message: str) -> None:
    raise SystemExit(message)


def read_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read platform manifest {path}: {error}")
    required = {
        "board", "usb_id", "usb_serial", "jtag_ids", "device",
        "quartus_version", "quartus_license_feature", "ghrd_archive",
        "ghrd_sha256", "ghrd_qsf", "ghrd_qsf_sha256", "ghrd_hps_hex",
        "ghrd_hps_hex_sha256", "resource_archive", "resource_sha256",
        "lpddr4b_ip", "lpddr4b_ip_sha256", "lpddr4b_calibration",
        "lpddr4b_calibration_sha256", "lpddr4b_reference_qsf",
        "lpddr4b_reference_qsf_sha256",
    }
    missing = sorted(required - manifest.keys())
    if missing:
        fail("platform manifest missing: " + ", ".join(missing))
    return manifest


def output(command: list[str]) -> str:
    try:
        return subprocess.run(
            command, check=True, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot run {' '.join(map(str, command))}: {error}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        fail(f"cannot read {path}: {error}")
    return digest.hexdigest()


def usb_serials(root: Path, usb_id: str) -> set[str]:
    vendor, product = usb_id.split(":", 1)
    serials = set()
    for device in root.iterdir():
        try:
            if ((device / "idVendor").read_text().strip().lower() == vendor and
                    (device / "idProduct").read_text().strip().lower() == product):
                serials.add((device / "serial").read_text().strip())
        except (FileNotFoundError, NotADirectoryError, PermissionError):
            continue
    return serials


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=HERE / "platform.json")
    parser.add_argument(
        "--quartus-root", type=Path,
        default=Path("/home/barry/altera_pro/26.1.1/quartus"),
    )
    parser.add_argument("--ghrd", type=Path, required=True)
    parser.add_argument("--resource", type=Path)
    parser.add_argument("--license", type=Path, required=True)
    parser.add_argument("--restored", type=Path)
    parser.add_argument(
        "--usb-root", type=Path, default=Path("/sys/bus/usb/devices"),
    )
    args = parser.parse_args()
    manifest = read_manifest(args.manifest)

    if args.ghrd.name != manifest["ghrd_archive"]:
        fail(f"wrong GHRD archive name: {args.ghrd.name}")
    actual_hash = sha256(args.ghrd)
    if actual_hash != manifest["ghrd_sha256"]:
        fail(
            "GHRD SHA-256 mismatch: "
            f"expected {manifest['ghrd_sha256']}, got {actual_hash}"
        )

    if args.resource:
        if args.resource.name != manifest["resource_archive"]:
            fail(f"wrong resource package name: {args.resource.name}")
        resource_hash = sha256(args.resource)
        if resource_hash != manifest["resource_sha256"]:
            fail(
                "resource package SHA-256 mismatch: "
                f"expected {manifest['resource_sha256']}, got {resource_hash}"
            )

    quartus_version = output([
        str(args.quartus_root / "bin" / "quartus_sh"), "--version",
    ])
    if manifest["quartus_version"] not in quartus_version:
        fail("unqualified Quartus version: " + quartus_version.splitlines()[0])

    try:
        license_text = args.license.read_text(encoding="ascii", errors="ignore")
    except OSError as error:
        fail(f"cannot read Quartus license {args.license}: {error}")
    if manifest["quartus_license_feature"] not in license_text:
        fail(
            "Quartus license lacks " + manifest["quartus_license_feature"]
        )

    if args.restored:
        for label, relative, expected in (
            ("GHRD QSF", manifest["ghrd_qsf"], manifest["ghrd_qsf_sha256"]),
            ("GHRD HPS boot hex", manifest["ghrd_hps_hex"],
             manifest["ghrd_hps_hex_sha256"]),
        ):
            actual = sha256(args.restored / relative)
            if actual != expected:
                fail(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")

    jtag = output([str(args.quartus_root / "bin" / "jtagconfig")])
    if manifest["board"] not in jtag:
        fail(f"{manifest['board']} is not present in the JTAG chain")
    missing_jtag = [value for value in manifest["jtag_ids"] if value not in jtag]
    if missing_jtag:
        fail("JTAG chain missing IDs: " + ", ".join(missing_jtag))

    try:
        serials = usb_serials(args.usb_root, manifest["usb_id"])
    except (OSError, ValueError) as error:
        fail(f"cannot inspect USB devices: {error}")
    if manifest["usb_serial"] not in serials:
        fail(
            f"DE25 USB serial {manifest['usb_serial']} not present; found "
            + (", ".join(sorted(serials)) or "none")
        )

    print("DE25 platform qualification: PASS")
    print(f"board={manifest['board']} serial={manifest['usb_serial']}")
    print(f"device={manifest['device']}")
    print(f"ghrd_sha256={actual_hash}")


if __name__ == "__main__":
    main()
