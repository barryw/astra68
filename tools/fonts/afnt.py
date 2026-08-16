#!/usr/bin/env python3
"""Import bitmap strikes into AFNT and emit trusted ROM C tables."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path


HEADER = struct.Struct(">4sHHHHIIII")
DIRECTORY = struct.Struct(">4sIIII")
STRIKE = struct.Struct(">HHhhhHIIIII")
GLYPH = struct.Struct(">IIIHHHHiii")
CMAP = struct.Struct(">II")
CHUNKS = (b"NAME", b"CMAP", b"STRK", b"GLYP", b"BITM")
MASK1 = 1


def align4(value: int) -> int:
    return (value + 3) & ~3


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def code_label(glyph) -> int | None:
    for label in glyph.get_labels():
        if label.__class__.__name__ == "Codepoint":
            raw = bytes(label)
            if not raw:
                raise ValueError("empty source-font codepoint")
            return int.from_bytes(raw, "big")
    return None


def pack_bitmap(glyph) -> tuple[bytes, int]:
    matrix = glyph.as_matrix()
    width = glyph.width
    pitch = (width + 7) // 8
    packed = bytearray()
    for row in matrix:
        bits = 0
        for x, pixel in enumerate(row):
            if pixel:
                bits |= 0x80 >> (x & 7)
            if (x & 7) == 7:
                packed.append(bits)
                bits = 0
        if width & 7:
            packed.append(bits)
    if len(packed) != pitch * glyph.height:
        raise ValueError("bitmap packing mismatch")
    return bytes(packed), pitch


def load_bitmap(path: Path):
    try:
        import monobit
    except ImportError as error:
        raise SystemExit(
            "bitmap import needs monobit 0.53; run with "
            "`uv run --with monobit==0.53 tools/fonts/afnt.py ...`"
        ) from error
    pack = monobit.load(path)
    if len(pack) != 1:
        raise ValueError(f"{path}: expected one font, found {len(pack)}")
    return pack[0]


def build_afnt(args: argparse.Namespace) -> bytes:
    fonts = [load_bitmap(path) for path in args.strikes]
    unicode_labels = args.unicode_labels
    expected_codes = (None if unicode_labels else
                      set(range(args.first_code, args.last_code + 1)))
    source_codes: list[int] | None = None
    glyph_sets = []

    for path, font in zip(args.strikes, fonts, strict=True):
        encoded = {code_label(glyph): glyph for glyph in font.glyphs
                   if code_label(glyph) is not None}
        if args.monospaced and len({glyph.advance_width
                                    for glyph in encoded.values()}) != 1:
            raise ValueError(f"{path}: glyph advances are not monospaced")
        if expected_codes is not None and set(encoded) != expected_codes:
            missing = sorted(expected_codes - set(encoded))
            extra = sorted(set(encoded) - expected_codes)
            raise ValueError(f"{path}: incomplete repertoire; missing={missing} extra={extra}")
        if font.get_default_glyph() is None:
            raise ValueError(f"{path}: missing default glyph")
        codes = sorted(encoded)
        if source_codes is not None and codes != source_codes:
            raise ValueError("strike repertoires differ")
        source_codes = codes
        fallback = encoded.get(0xFFFD if unicode_labels else ord("?"),
                               font.get_default_glyph())
        glyph_sets.append((encoded, fallback))

    assert source_codes is not None
    unicode_to_id = []
    for glyph_id, source_code in enumerate(source_codes):
        scalar = (source_code if unicode_labels else
                  ord(bytes((source_code,)).decode(args.encoding)))
        unicode_to_id.append((scalar, glyph_id))
    replacement_id = len(source_codes)
    unicode_to_id.append((0xFFFD, replacement_id))
    unicode_to_id.sort()

    cmap = struct.pack(">I", len(unicode_to_id)) + b"".join(
        CMAP.pack(scalar, glyph_id) for scalar, glyph_id in unicode_to_id
    )
    bitmap = bytearray()
    glyph_records = []
    strike_records = []
    glyph_first = 0

    for strike_id, ((encoded, default), font) in enumerate(
            zip(glyph_sets, fonts, strict=True)):
        ordered = [encoded[code] for code in source_codes] + [default]
        for glyph_id, glyph in enumerate(ordered):
            pixels, pitch = pack_bitmap(glyph)
            offset = len(bitmap)
            bitmap.extend(pixels)
            glyph_records.append(GLYPH.pack(
                glyph_id, offset, len(pixels), glyph.width, glyph.height,
                pitch, 0, glyph.left_bearing * 64,
                (glyph.shift_up + glyph.height) * 64,
                glyph.advance_width * 64,
            ))
        strike_records.append(STRIKE.pack(
            strike_id, font.line_height, font.ascent, font.descent,
            font.leading, MASK1, glyph_first, len(ordered), GLYPH.size,
            0, 0,
        ))
        glyph_first += len(ordered)

    metadata = {
        "family": args.family,
        "style": args.style,
        "license": args.license,
        "source_encoding": "unicode" if unicode_labels else args.encoding,
        "source_revision": args.source_revision,
        "source_files": [
            {"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in args.strikes
        ],
        "glyphs_per_strike": len(source_codes) + 1,
    }
    chunks = {
        b"NAME": json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode(),
        b"CMAP": cmap,
        b"STRK": struct.pack(">I", len(strike_records)) + b"".join(strike_records),
        b"GLYP": struct.pack(">I", len(glyph_records)) + b"".join(glyph_records),
        b"BITM": bytes(bitmap),
    }
    directory_offset = HEADER.size
    payload_offset = align4(directory_offset + len(CHUNKS) * DIRECTORY.size)
    entries = []
    payload = bytearray()
    cursor = payload_offset
    for kind in CHUNKS:
        data = chunks[kind]
        cursor = align4(cursor)
        while payload_offset + len(payload) < cursor:
            payload.append(0)
        entries.append(DIRECTORY.pack(kind, 0, cursor, len(data), crc32(data)))
        payload.extend(data)
        cursor += len(data)
    directory = b"".join(entries)
    total = payload_offset + len(payload)
    header = HEADER.pack(b"AFNT", 0, 1, HEADER.size, DIRECTORY.size,
                         len(CHUNKS), total, crc32(directory), 0)
    return header + directory + bytes(payload_offset - HEADER.size - len(directory)) + payload


def parse_afnt(data: bytes) -> dict[bytes, bytes]:
    if len(data) < HEADER.size:
        raise ValueError("truncated AFNT header")
    magic, major, minor, header_size, entry_size, count, total, directory_crc, flags = HEADER.unpack_from(data)
    if (magic, major, minor, header_size, entry_size, total, flags) != (
            b"AFNT", 0, 1, HEADER.size, DIRECTORY.size, len(data), 0):
        raise ValueError("invalid AFNT header")
    directory_end = header_size + count * entry_size
    if directory_end > len(data):
        raise ValueError("truncated AFNT directory")
    directory = data[header_size:directory_end]
    if crc32(directory) != directory_crc:
        raise ValueError("AFNT directory CRC mismatch")
    chunks = {}
    for index in range(count):
        kind, flags, offset, length, checksum = DIRECTORY.unpack_from(
            directory, index * entry_size)
        if flags != 0 or kind in chunks or offset & 3 or offset > len(data) or length > len(data) - offset:
            raise ValueError("invalid AFNT directory entry")
        chunk = data[offset:offset + length]
        if crc32(chunk) != checksum:
            raise ValueError(f"{kind.decode(errors='replace')} CRC mismatch")
        chunks[kind] = chunk
    if set(chunks) != set(CHUNKS):
        raise ValueError("AFNT required chunk set mismatch")
    return chunks


def unpack_afnt(data: bytes):
    chunks = parse_afnt(data)
    cmap_data = chunks[b"CMAP"]
    strike_data = chunks[b"STRK"]
    glyph_data = chunks[b"GLYP"]
    if len(cmap_data) < 4 or len(strike_data) < 4 or len(glyph_data) < 4:
        raise ValueError("truncated AFNT record chunk")
    cmap_count = struct.unpack_from(">I", cmap_data)[0]
    strike_count = struct.unpack_from(">I", strike_data)[0]
    glyph_count = struct.unpack_from(">I", glyph_data)[0]
    if len(cmap_data) != 4 + cmap_count * CMAP.size:
        raise ValueError("invalid CMAP length")
    if len(strike_data) != 4 + strike_count * STRIKE.size:
        raise ValueError("invalid STRK length")
    if len(glyph_data) != 4 + glyph_count * GLYPH.size:
        raise ValueError("invalid GLYP length")
    cmap = [CMAP.unpack_from(cmap_data, 4 + index * CMAP.size)
            for index in range(cmap_count)]
    strikes = [STRIKE.unpack_from(strike_data, 4 + index * STRIKE.size)
               for index in range(strike_count)]
    glyphs = [GLYPH.unpack_from(glyph_data, 4 + index * GLYPH.size)
              for index in range(glyph_count)]
    if cmap != sorted(cmap) or len({scalar for scalar, _ in cmap}) != len(cmap):
        raise ValueError("CMAP must be unique and sorted")
    for strike_id, height, ascent, descent, leading, bitmap_format, first, count, record_size, reserved0, reserved1 in strikes:
        if (strike_id >= strike_count or height == 0 or ascent < 0 or descent < 0 or
                bitmap_format != MASK1 or record_size != GLYPH.size or
                first > glyph_count or count > glyph_count - first or reserved0 or reserved1):
            raise ValueError("invalid strike record")
        ids = [record[0] for record in glyphs[first:first + count]]
        if ids != list(range(count)):
            raise ValueError("glyph IDs are not dense")
    bitmap = chunks[b"BITM"]
    for _, offset, length, width, height, pitch, flags, _, _, advance in glyphs:
        if (width == 0 or height == 0 or pitch != (width + 7) // 8 or flags != 0 or
                offset > len(bitmap) or length != pitch * height or
                length > len(bitmap) - offset or advance <= 0):
            raise ValueError("invalid glyph record")
    json.loads(chunks[b"NAME"])
    return cmap, strikes, glyphs, bitmap


def emit_array(out, name: str, values, ctype: str, width: int = 8) -> None:
    out.write(f"static const {ctype} {name}[] = {{\n")
    for start in range(0, len(values), width):
        row = values[start:start + width]
        out.write("    " + ", ".join(str(value) for value in row) + ",\n")
    out.write("};\n\n")


def emit_c(data: bytes, output: Path, prefix: str) -> None:
    cmap, strikes, glyphs, bitmap = unpack_afnt(data)
    lines = []
    from io import StringIO
    out = StringIO()
    out.write("/* Generated by tools/fonts/afnt.py; do not edit. */\n")
    emit_array(out, f"{prefix}_cmap_codepoints", [entry[0] for entry in cmap], "uint32_t")
    emit_array(out, f"{prefix}_cmap_glyphs", [entry[1] for entry in cmap], "uint16_t")
    out.write(f"static const AstraUiStrike {prefix}_strikes[] = {{\n")
    for _, height, ascent, descent, leading, _, first, count, _, _, _ in strikes:
        out.write(f"    {{ {height}u, {ascent}u, {descent}u, {leading}u, {first}u, {count}u }},\n")
    out.write(f"}};\n\nstatic const AstraUiGlyph {prefix}_glyphs[] = {{\n")
    for glyph_id, offset, length, width, height, pitch, _, bearing_x, bearing_y, advance in glyphs:
        out.write(f"    {{ {offset}u, {length}u, {width}u, {height}u, {pitch}u, "
                  f"{bearing_x}, {bearing_y}, {advance} }}, /* {glyph_id} */\n")
    out.write("};\n\n")
    emit_array(out, f"{prefix}_bitmap", list(bitmap), "uint8_t", 12)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(out.getvalue(), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    importer = commands.add_parser("import-amiga", aliases=["import-bitmap"])
    importer.add_argument("--family", required=True)
    importer.add_argument("--style", default="Regular")
    importer.add_argument("--license", required=True)
    importer.add_argument("--source-revision", required=True)
    importer.add_argument("--encoding", default="latin-1")
    importer.add_argument("--first-code", type=int, default=0)
    importer.add_argument("--last-code", type=int, default=255)
    importer.add_argument("--unicode-labels", action="store_true")
    importer.add_argument("--monospaced", action="store_true")
    importer.add_argument("--output", type=Path, required=True)
    importer.add_argument("strikes", nargs="+", type=Path)
    validator = commands.add_parser("validate")
    validator.add_argument("input", type=Path)
    emitter = commands.add_parser("emit-c")
    emitter.add_argument("input", type=Path)
    emitter.add_argument("output", type=Path)
    emitter.add_argument("--prefix", default="astra_ui")
    args = parser.parse_args()
    try:
        if args.command in ("import-amiga", "import-bitmap"):
            data = build_afnt(args)
            unpack_afnt(data)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(data)
        elif args.command == "validate":
            cmap, strikes, glyphs, bitmap = unpack_afnt(args.input.read_bytes())
            print(f"AFNT PASS cmap={len(cmap)} strikes={len(strikes)} "
                  f"glyphs={len(glyphs)} bitmap={len(bitmap)}")
        else:
            emit_c(args.input.read_bytes(), args.output, args.prefix)
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
        print(f"afnt: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
