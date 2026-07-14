import struct
from pathlib import Path

import pytest

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from make_fat32_image import build_image


def sector(image: bytes, lba: int) -> bytes:
    offset = lba * 512
    return image[offset : offset + 512]


def test_builds_minimal_fat32_boot_volume(tmp_path: Path) -> None:
    rom = bytes((index * 37) & 0xFF for index in range(1300))
    rom_path = tmp_path / "ASTRA68.ROM"
    image_path = tmp_path / "sdcard.img"
    rom_path.write_bytes(rom)

    geometry = build_image(rom_path, image_path)
    image = image_path.read_bytes()
    partition = geometry["partition_start"]
    boot = sector(image, partition)

    assert sector(image, 0)[510:512] == b"\x55\xaa"
    assert sector(image, 0)[446 + 4] == 0x0C
    assert struct.unpack_from("<I", sector(image, 0), 446 + 8)[0] == partition
    assert boot[510:512] == b"\x55\xaa"
    assert struct.unpack_from("<H", boot, 11)[0] == 512
    assert boot[13] == 1
    assert struct.unpack_from("<I", boot, 44)[0] == 2

    root = sector(image, geometry["data_lba"])
    assert root[0:11] == b"ASTRA68 ROM"
    assert struct.unpack_from("<I", root, 28)[0] == len(rom)
    assert image[geometry["file_lba"] * 512 :
                 geometry["file_lba"] * 512 + len(rom)] == rom

    fat_lba = partition + 32
    fat = sector(image, fat_lba)
    assert struct.unpack_from("<I", fat, 8)[0] == 0x0FFFFFFF
    assert struct.unpack_from("<I", fat, 12)[0] == 4
    assert struct.unpack_from("<I", fat, 16)[0] == 5
    assert struct.unpack_from("<I", fat, 20)[0] == 0x0FFFFFFF


def test_rejects_non_fat32_partition(tmp_path: Path) -> None:
    rom_path = tmp_path / "ASTRA68.ROM"
    rom_path.write_bytes(b"test")
    with pytest.raises(ValueError, match="too small for FAT32"):
        build_image(rom_path, tmp_path / "small.img", partition_sectors=1000)
