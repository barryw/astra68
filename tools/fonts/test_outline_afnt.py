#!/usr/bin/env python3
"""Contract test for complete outline-font to A8 AFNT conversion."""

from argparse import Namespace
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from fontTools.ttLib import TTCollection, TTFont
from fontTools.colorLib.builder import buildCOLR, buildCPAL


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/fonts/afnt.py"
spec = importlib.util.spec_from_file_location("afnt", TOOL)
assert spec is not None and spec.loader is not None
afnt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(afnt)


def convert(path: Path, family: str, revision: str, strikes,
            monospaced=False, face_index=0):
    return afnt.build_outline_afnt(Namespace(
        input=path,
        family=family,
        style="Regular",
        license="OFL-1.1",
        source_revision=revision,
        strikes=strikes,
        monospaced=monospaced,
        face_index=face_index,
    ))


atkinson = ROOT / (
    "sw/userspace/graphics/fonts/atkinson-hyperlegible-next/"
    "AtkinsonHyperlegibleNext-Regular.ttf")
atkinson_data = convert(
    atkinson, "Astra Sans",
    "7925f50f649b3813257faf2f4c0b381011f434f1",
    [(11, 8), (13, 10), (16, 13)])
assert atkinson_data == (ROOT / "sw/userspace/graphics/fonts/"
                          "astra-workbench.afnt").read_bytes()
cmap, strikes, glyphs, bitmap = afnt.unpack_afnt(atkinson_data)
codepoints = {scalar for scalar, _ in cmap}
assert len(cmap) >= 350
assert {ord("A"), 0x00e9, 0x2014, 0xfffd} <= codepoints
assert len(strikes) == 3
assert all(strike[1] == afnt.A8 for strike in strikes)
assert all(record[5] == record[3] for record in glyphs)
assert any(0 < coverage < 255 for coverage in bitmap)
metadata = json.loads(afnt.parse_afnt(atkinson_data)[b"NAME"])
assert metadata["family"] == "Astra Sans"
assert metadata["outline_ppem"] == [8, 10, 13]

jetbrains = ROOT / (
    "sw/userspace/graphics/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf")
jetbrains_data = convert(
    jetbrains, "Astra Mono", "v2.304", [(16, 13)], monospaced=True)
assert jetbrains_data == (ROOT / "sw/userspace/graphics/fonts/"
                           "astra-mono.afnt").read_bytes()
cmap, strikes, glyphs, bitmap = afnt.unpack_afnt(jetbrains_data)
assert len(cmap) >= 1300
assert len(strikes) == 1 and strikes[0][1] == afnt.A8
assert strikes[0][2] == 8 and strikes[0][9] == 8 * 64
assert all(record[9] in (0, 8 * 64) for record in glyphs)
assert any(0 < coverage < 255 for coverage in bitmap)

# Pitch validation is format-aware; an A8 row is one byte per pixel.
chunks = afnt.parse_afnt(atkinson_data)
bad_glyphs = list(glyphs := afnt.unpack_afnt(atkinson_data)[2])
bad_glyphs[0] = (*bad_glyphs[0][:5], bad_glyphs[0][5] + 1,
                 *bad_glyphs[0][6:])
bad_data = afnt.serialize_afnt(
    json.loads(chunks[b"NAME"]), chunks[b"CMAP"],
    [afnt.STRIKE.pack(*record)
     for record in afnt.unpack_afnt(atkinson_data)[1]],
    [afnt.GLYPH.pack(*record) for record in bad_glyphs], chunks[b"BITM"])
try:
    afnt.unpack_afnt(bad_data)
except ValueError as error:
    assert str(error) == "invalid glyph record"
else:
    raise AssertionError("invalid A8 pitch was accepted")

with TemporaryDirectory() as directory:
    collection_path = Path(directory) / "mixed.ttc"
    collection = TTCollection()
    collection.fonts = [TTFont(atkinson), TTFont(jetbrains)]
    try:
        collection.save(collection_path)
    finally:
        collection.close()
    imported = convert(collection_path, "Astra Mono", "test",
                       [(16, 13)], monospaced=True, face_index=1)
    imported_cmap, imported_strikes, _, _ = afnt.unpack_afnt(imported)
    assert len(imported_cmap) >= 1300
    assert imported_strikes[0][2] == 8
    assert json.loads(afnt.parse_afnt(imported)[b"NAME"])[
        "source_face_index"] == 1
    for path, index in ((collection_path, 2), (atkinson, 1)):
        try:
            convert(path, "Invalid", "test", [(16, 13)], face_index=index)
        except ValueError as error:
            assert "face index" in str(error)
        else:
            raise AssertionError("invalid collection face index was accepted")

    color_path = Path(directory) / "color.ttf"
    color_face = TTFont(atkinson)
    color_face["COLR"] = buildCOLR({"A": [("A", 0)]})
    color_face["CPAL"] = buildCPAL([[(0, 0, 0, 1)]])
    color_face.save(color_path)
    color_face.close()
    try:
        convert(color_path, "Color", "test", [(16, 13)])
    except ValueError as error:
        assert "color glyphs" in str(error)
    else:
        raise AssertionError("color font was silently reduced to A8")

print("AFNT outline-font contract: PASS")
