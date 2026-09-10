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
STRIKE = struct.Struct(">HHHHiiiiiiiiiiIIIII")
GLYPH = struct.Struct(">IIIHHHHiii")
CMAP = struct.Struct(">II")
CHUNKS = (b"NAME", b"CMAP", b"STRK", b"GLYP", b"BITM")
MASK1 = 1

# CP437 bytes 1..31 are printable symbols, unlike the Unicode C0 controls.
# Spleen uses the standard arrow/corner forms for the three pointer variants
# it does not encode separately.
CP437_LOW = (
    0x20, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x2192, 0x2190, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x2514, 0x2194, 0x25B2, 0x25BC,
)


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
        cap_height = max(0, encoded.get(ord("H"), default).shift_up +
                         encoded.get(ord("H"), default).height)
        x_height = max(0, encoded.get(ord("x"), default).shift_up +
                       encoded.get(ord("x"), default).height)
        max_advance = max(glyph.advance_width for glyph in ordered)
        thickness = max(1, (font.line_height + 15) // 16)
        underline_position = max(1, (font.descent + 1) // 2)
        strikeout_position = max(1, x_height // 2 if x_height else
                                 font.ascent // 3)
        strike_records.append(STRIKE.pack(
            strike_id, MASK1,
            max_advance if args.monospaced else 0, font.line_height,
            font.ascent * 64, font.descent * 64, font.leading * 64,
            cap_height * 64, x_height * 64, max_advance * 64,
            underline_position * 64, thickness * 64,
            strikeout_position * 64, thickness * 64,
            glyph_first, len(ordered), GLYPH.size, 0, 0,
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
    header = HEADER.pack(b"AFNT", 0, 2, HEADER.size, DIRECTORY.size,
                         len(CHUNKS), total, crc32(directory), 0)
    return header + directory + bytes(payload_offset - HEADER.size - len(directory)) + payload


def parse_afnt(data: bytes) -> dict[bytes, bytes]:
    if len(data) < HEADER.size:
        raise ValueError("truncated AFNT header")
    magic, major, minor, header_size, entry_size, count, total, directory_crc, flags = HEADER.unpack_from(data)
    if (magic, major, minor, header_size, entry_size, total, flags) != (
        b"AFNT", 0, 2, HEADER.size, DIRECTORY.size, len(data), 0):
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
    for (strike_id, bitmap_format, pixel_width, pixel_height, ascent,
         descent, line_gap, cap_height, x_height, max_advance,
         underline_position, underline_thickness, strikeout_position,
         strikeout_thickness, first, count, record_size, flags,
         reserved) in strikes:
        if (strike_id >= strike_count or pixel_height == 0 or
                ascent < 0 or descent < 0 or line_gap < 0 or
                cap_height < 0 or x_height < 0 or max_advance <= 0 or
                underline_thickness <= 0 or strikeout_thickness <= 0 or
                bitmap_format != MASK1 or record_size != GLYPH.size or
                first > glyph_count or count > glyph_count - first or
                flags or reserved or (pixel_width and pixel_width * 64 != max_advance)):
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
    for (strike_id, bitmap_format, pixel_width, pixel_height, ascent,
         descent, line_gap, cap_height, x_height, max_advance,
         underline_position, underline_thickness, strikeout_position,
         strikeout_thickness, first, count, record_size, flags,
         reserved) in strikes:
        del strike_id, record_size, flags, reserved
        out.write(
            f"    {{ {pixel_width}u, {pixel_height}u, {bitmap_format}u, 0u, "
            f"{ascent}, {descent}, {line_gap}, {cap_height}, {x_height}, "
            f"{max_advance}, {underline_position}, {underline_thickness}, "
            f"{strikeout_position}, {strikeout_thickness}, {first}u, "
            f"{count}u }},\n")
    out.write(f"}};\n\nstatic const AstraUiGlyph {prefix}_glyphs[] = {{\n")
    for glyph_id, offset, length, width, height, pitch, _, bearing_x, bearing_y, advance in glyphs:
        out.write(f"    {{ {offset}u, {length}u, {width}u, {height}u, {pitch}u, "
                  f"{bearing_x}, {bearing_y}, {advance} }}, /* {glyph_id} */\n")
    out.write("};\n\n")
    emit_array(out, f"{prefix}_bitmap", list(bitmap), "uint8_t", 12)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(out.getvalue(), encoding="ascii")


def emit_cp437_hex(data: bytes) -> str:
    cmap, strikes, glyphs, bitmap = unpack_afnt(data)
    if len(strikes) != 1:
        raise ValueError("rescue font must contain exactly one strike")
    (_, bitmap_format, pixel_width, pixel_height, ascent, descent, line_gap,
     _cap_height, _x_height, max_advance, _underline_position,
     _underline_thickness, _strikeout_position, _strikeout_thickness,
     first, count, _record_size, _flags, _reserved) = strikes[0]
    if (bitmap_format, pixel_width, pixel_height, ascent, descent, line_gap,
            max_advance) != (MASK1, 8, 16, 12 * 64, 4 * 64, 0, 8 * 64):
        raise ValueError("rescue font must use 8x16 Spleen line metrics")

    glyph_ids = {scalar: glyph_id for scalar, glyph_id in cmap}
    scalars = list(CP437_LOW)
    scalars.extend(ord(bytes((value,)).decode("cp437"))
                   for value in range(32, 127))
    scalars.append(0x2302)
    scalars.extend(ord(bytes((value,)).decode("cp437"))
                   for value in range(128, 256))
    output = bytearray()
    for scalar in scalars:
        glyph_id = glyph_ids.get(scalar)
        if glyph_id is None or glyph_id >= count:
            raise ValueError(f"rescue font lacks U+{scalar:04X}")
        (_, offset, length, width, glyph_height, pitch, flags,
         bearing_x, bearing_y, advance) = glyphs[first + glyph_id]
        if (width, glyph_height, pitch, flags, bearing_x, bearing_y,
                advance, length) != (8, 16, 1, 0, 0, 12 * 64, 8 * 64, 16):
            raise ValueError(f"U+{scalar:04X} violates rescue-cell metrics")
        output.extend(bitmap[offset:offset + length])
    return "".join(f"{value:02x}\n" for value in output)


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
    rescue = commands.add_parser("emit-cp437-hex")
    rescue.add_argument("input", type=Path)
    rescue.add_argument("output", type=Path)
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
        elif args.command == "emit-c":
            emit_c(args.input.read_bytes(), args.output, args.prefix)
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                emit_cp437_hex(args.input.read_bytes()), encoding="ascii")
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
        print(f"afnt: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
