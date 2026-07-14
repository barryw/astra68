#!/usr/bin/env python3
"""Build the Astra 68 SD-card boot image used by simulation and provisioning."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


SECTOR_SIZE = 512
PARTITION_START = 2048
PARTITION_SECTORS = 131072
RESERVED_SECTORS = 32
FAT_COUNT = 2
ROOT_CLUSTER = 2
FILE_CLUSTER = 3
BOOT_FILENAME = b"ASTRA68 ROM"


def put_sector(output, lba: int, data: bytes) -> None:
    if len(data) != SECTOR_SIZE:
        raise ValueError("sector must contain exactly 512 bytes")
    output.seek(lba * SECTOR_SIZE)
    output.write(data)


def fat_geometry(total_sectors: int) -> tuple[int, int]:
    if total_sectors <= RESERVED_SECTORS:
        raise ValueError("partition is too small")
    fat_sectors = math.ceil(
        (total_sectors - RESERVED_SECTORS) * 4 /
        (SECTOR_SIZE + FAT_COUNT * 4)
    )
    while True:
        data_sectors = total_sectors - RESERVED_SECTORS - FAT_COUNT * fat_sectors
        if data_sectors <= 0:
            raise ValueError("partition is too small")
        cluster_count = data_sectors
        if fat_sectors * SECTOR_SIZE >= (cluster_count + 2) * 4:
            return fat_sectors, cluster_count
        fat_sectors += 1


def build_image(
    rom_path: Path,
    output_path: Path,
    partition_start: int = PARTITION_START,
    partition_sectors: int = PARTITION_SECTORS,
) -> dict[str, int]:
    rom = rom_path.read_bytes()
    if not rom:
        raise ValueError("ROM package is empty")

    fat_sectors, cluster_count = fat_geometry(partition_sectors)
    if cluster_count < 65525:
        raise ValueError("partition is too small for FAT32")

    file_clusters = math.ceil(len(rom) / SECTOR_SIZE)
    if FILE_CLUSTER + file_clusters > cluster_count + 2:
        raise ValueError("ROM package does not fit in the boot partition")

    fat_lba = partition_start + RESERVED_SECTORS
    data_lba = fat_lba + FAT_COUNT * fat_sectors

    mbr = bytearray(SECTOR_SIZE)
    entry = 446
    mbr[entry + 1 : entry + 4] = b"\xfe\xff\xff"
    mbr[entry + 4] = 0x0C
    mbr[entry + 5 : entry + 8] = b"\xfe\xff\xff"
    struct.pack_into("<II", mbr, entry + 8, partition_start, partition_sectors)
    mbr[510:512] = b"\x55\xaa"

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xeb\x58\x90"
    boot[3:11] = b"ASTRA68 "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR_SIZE, 1, RESERVED_SECTORS, FAT_COUNT,
                     0, 0, 0xF8, 0, 63, 255, partition_start,
                     partition_sectors)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0,
                     ROOT_CLUSTER, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0xA5680300)
    boot[71:82] = b"ASTRA68BOOT"
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"

    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<II", fsinfo, 488, 0xFFFFFFFF,
                     FILE_CLUSTER + file_clusters)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
    for index in range(file_clusters):
        cluster = FILE_CLUSTER + index
        value = 0x0FFFFFFF if index == file_clusters - 1 else cluster + 1
        struct.pack_into("<I", fat, cluster * 4, value)

    root = bytearray(SECTOR_SIZE)
    root[0:11] = BOOT_FILENAME
    root[11] = 0x20
    struct.pack_into("<H", root, 20, FILE_CLUSTER >> 16)
    struct.pack_into("<H", root, 26, FILE_CLUSTER & 0xFFFF)
    struct.pack_into("<I", root, 28, len(rom))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as output:
        put_sector(output, 0, mbr)
        put_sector(output, partition_start, boot)
        put_sector(output, partition_start + 1, fsinfo)
        put_sector(output, partition_start + 6, boot)
        for copy in range(FAT_COUNT):
            output.seek((fat_lba + copy * fat_sectors) * SECTOR_SIZE)
            output.write(fat)
        put_sector(output, data_lba, root)
        output.seek((data_lba + FILE_CLUSTER - ROOT_CLUSTER) * SECTOR_SIZE)
        output.write(rom)
        padding = (-len(rom)) % SECTOR_SIZE
        if padding:
            output.write(bytes(padding))

    return {
        "partition_start": partition_start,
        "partition_sectors": partition_sectors,
        "fat_sectors": fat_sectors,
        "data_lba": data_lba,
        "file_lba": data_lba + FILE_CLUSTER - ROOT_CLUSTER,
        "file_size": len(rom),
        "image_size": output_path.stat().st_size,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path, help="packaged ASTRA68.ROM input")
    parser.add_argument("output", type=Path, help="sparse raw card image")
    parser.add_argument("--partition-start", type=int, default=PARTITION_START)
    parser.add_argument("--partition-sectors", type=int, default=PARTITION_SECTORS)
    args = parser.parse_args()

    geometry = build_image(args.rom, args.output, args.partition_start,
                           args.partition_sectors)
    print(
        f"wrote {args.output}: FAT32 LBA {geometry['partition_start']}, "
        f"ASTRA68.ROM {geometry['file_size']} bytes at LBA "
        f"{geometry['file_lba']}, image {geometry['image_size']} bytes"
    )


if __name__ == "__main__":
    main()
