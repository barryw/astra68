import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import event_catalog  # noqa: E402
import system_event_catalog  # noqa: E402


def descriptor(line):
    record = struct.pack(">IHBBB4s3x", event_catalog.DESCRIPTOR_MAGIC, line,
                         2, 3, 1, bytes((1, 0, 0, 0)))
    record += b"source.c".ljust(event_catalog.FILE_MAX, b"\0")
    record += b"status %u".ljust(event_catalog.FORMAT_MAX, b"\0")
    return record


def build_elf(path, section, base):
    names = b"\0.astra_events\0.shstrtab\0"
    header_size, entry_size = 52, 40
    names_offset = header_size + len(section)
    table_offset = names_offset + len(names)
    elf = bytearray()
    elf += b"\x7fELF" + bytes((1, 2, 1)) + bytes(9)
    elf += struct.pack(">HHI", 2, 4, 1)
    elf += struct.pack(">III", 0, 0, table_offset)
    elf += struct.pack(">IHHHHHH", 0, header_size, 0, 0, entry_size, 3, 2)
    elf += section
    elf += names

    def entry(name, kind, address, offset, size):
        return struct.pack(">IIIIIIIIII", name, kind, 0, address, offset,
                           size, 0, 0, 4, 0)

    elf += entry(0, 0, 0, 0, 0)
    elf += entry(1, 1, base, header_size, len(section))
    elf += entry(15, 3, 0, names_offset, len(names))
    path.write_bytes(elf)
    return path


class SystemEventCatalogTests(unittest.TestCase):
    def test_linker_base_follows_the_existing_dense_catalog(self):
        existing = descriptor(1) + descriptor(2)
        self.assertEqual(system_event_catalog.next_base([existing]),
                         0xE0000100)
        self.assertEqual(system_event_catalog.linker_script([existing]),
                         "ASTRA_EVENT_IMAGE_CATALOG_BASE = 0xe0000100;\n")

    def test_elf_fragments_merge_in_message_id_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_blob = descriptor(1) + descriptor(2)
            second_blob = descriptor(3)
            first = build_elf(root / "supervisor.elf", first_blob, 0xE0000000)
            second = build_elf(root / "terminal.elf", second_blob, 0xE0000100)
            base, merged = system_event_catalog.merge_elf_catalogs(
                [first, second])
        self.assertEqual(base, 0xE0000000)
        self.assertEqual(merged, first_blob + second_blob)
        self.assertEqual([entry["line"] for entry in
                          event_catalog.parse(base, merged).values()],
                         [1, 2, 3])

    def test_a_gap_or_overlap_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = build_elf(root / "first.elf", descriptor(1), 0xE0000000)
            gap = build_elf(root / "gap.elf", descriptor(2), 0xE0000200)
            overlap = build_elf(root / "overlap.elf", descriptor(2),
                                0xE0000000)
            with self.assertRaisesRegex(system_event_catalog.SystemCatalogError,
                                        "contiguous"):
                system_event_catalog.merge_elf_catalogs([first, gap])
            with self.assertRaisesRegex(system_event_catalog.SystemCatalogError,
                                        "contiguous"):
                system_event_catalog.merge_elf_catalogs([first, overlap])

    def test_a_malformed_raw_fragment_is_refused(self):
        with self.assertRaisesRegex(system_event_catalog.SystemCatalogError,
                                    "descriptor"):
            system_event_catalog.next_base([b"not a descriptor"])


if __name__ == "__main__":
    unittest.main()
