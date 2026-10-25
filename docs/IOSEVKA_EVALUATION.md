# Iosevka evaluation — 2026-10-15

Status: AFNT conversion proved on Beast; runtime installation and system-role
selection are pending. This is not a system-wide font change.

Source: official [Iosevka](https://typeof.net/Iosevka/) 34.9.0
`PkgTTF-IosevkaFixed-34.9.0.zip`, SHA-256
`c3204f46863af9e7f0ae06f0bd0b3414697c424dba8573daf37c3053a76213b7`
(matches release digest). `IosevkaFixed-Regular.ttf` SHA-256:
`c90a6bcec3c3c5aa8e32c08a1e22db528185c53606604e1d1fcd30401a59312e`.
Upstream license: [SIL Open Font License 1.1](https://github.com/be5invis/Iosevka/blob/main/LICENSE.md).

Pinned build-time spike environment: fonttools 4.65.0, Pillow 12.3.0. Existing
`tools/fonts/afnt.py` imported complete Unicode cmap into a monospaced A8
16-pixel AFNT strike:

```sh
python tools/fonts/afnt.py import-outline \
  --family 'Iosevka Fixed' --style Regular --license OFL-1.1 \
  --source-revision v34.9.0 --monospaced --strike 16:13 \
  --output iosevka-fixed.afnt IosevkaFixed-Regular.ttf
python tools/fonts/afnt.py validate iosevka-fixed.afnt
```

Result: `AFNT PASS cmap=7735 strikes=1 glyphs=7735 bitmap=551142`.
Validated AFNT: 861,046 bytes, SHA-256
`46ab1af7e84c96b63a20b0339a90ea9b8a52726676022cdb628ce1a60d1299ed`.
The source ZIP, TTF, and generated AFNT are isolated under
`/home/barry/astra-font-spike-20261015/` on Beast, outside production images.
Existing AFNT rescue and outline contract tests pass in the pinned environment.

On 2026-10-16, reusable HPS C conversion code using FreeType converted the
same Iosevka Fixed, Aile, and Etoile TTF sources plus static Source Sans OTF
into AFNT. Shared `libastraafnt.a` validated its output. Beast host and
ASan/UBSan tests pass; the converter and codec compile for AArch64. This is
worker code, not an installed service or native UI import flow. Python above
remains a build-time comparison only and will not run when users import fonts.

The quasi-proportional Aile and Etoile regular faces also convert to AFNT at
11:8, 13:10, and 16:13 without a monospaced flag:

| Face | Official ZIP SHA-256 | TTF SHA-256 | AFNT SHA-256 | AFNT bytes | Validation |
|---|---|---|---|---:|---|
| Aile | `88ce8445d6a05899c897596e329621b422154f4baef98bd50e3afbe4c90dcff4` | `f12524621d2b21a83e8727b40d5d438b8671a6137c3808b44bf41ba00f6d383f` | `49c174f65a7fcecbc42cd5e1b9bdbd4c7b4701708a197794c6ee5b173b953ab1` | 2,338,757 | 7,758 cmap entries, 3 strikes, 23,274 glyph records |
| Etoile | `f25b34ffea64e2ecea90df13c075f11defd85708106128235837dfa039f446bb` | `4b19009ef685608fb597137af5d8408696f74c00a2755e5aed534b8197699920` | `35aa62ac7025f4fd27983fe1a70cce6852e4149a0dd3922797ba239d5e1a2cd4` | 2,335,580 | 7,758 cmap entries, 3 strikes, 23,274 glyph records |

Both ZIP hashes match official release digests. Complete Unicode coverage is
much larger than current resident Astra Sans (86,147-byte AFNT) and Astra Mono
(179,716-byte AFNT). No ROM substitution is warranted from this spike; installed
content and role-specific subset/cache policy need separate capacity review.

This proves conversion and native-format size only. It does not prove
legibility at each system role, hardware-rendered appearance, visual fit with
Atkinson UI, or latency. Compare UI, mono, title, and terminal specimens on
DE25 before changing assignments. Keep rescue font immutable.

For a usable Fonts tool, import must submit source bytes through the native
font-service transaction, convert in an unprivileged host worker, validate the
AFNT object, durably publish the catalog generation, then browse and preview
the installed face. File paths may label a source but cannot grant service
authority. Settings should compose Interface Kit List, Field, Button, Label,
and ScrollView controls. The shared List control first appears in Interface
Kit 5.5; no app-private list painter is needed.

Haiku references reviewed read-only:
`src/preferences/appearance/FontView.cpp`, `FontSelectionView.cpp`,
`src/servers/app/font/FontManager.cpp`, and `GlobalFontManager.cpp`. Adopted
separate UI composition from one shared font catalog. Astra adapts that split
to AFNT-only rendering and a Linux-side conversion worker; Haiku's FreeType
rendering path is not Astra's hardware glyph contract.
