from pathlib import Path

import pytest

from tools.generate_archive_contract import (
    explicit_symbols,
    omit_symbols,
    visible_definitions,
    write_undefined_response,
    write_version_map,
)


SYMBOLS = """
Symbol table '.symtab' contains 7 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     1: 00000000    12 FUNC    GLOBAL DEFAULT    3 puts
     2: 00000000     4 OBJECT  WEAK   PROTECTED  5 errno
     3: 00000000     8 FUNC    GLOBAL HIDDEN     3 private_helper
     4: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND memcpy
     5: 00000000     0 FILE    GLOBAL DEFAULT  ABS source.c
     6: 00000000     0 NOTYPE  GLOBAL DEFAULT  ABS ASTRA_LIBC_1_0
"""


def test_reads_only_visible_definitions() -> None:
    assert visible_definitions(SYMBOLS) == {"puts", "errno"}


def test_explicit_symbol_file_rejects_duplicates(tmp_path: Path) -> None:
    path = tmp_path / "extra.exports"
    path.write_text("read\nread\n", encoding="utf-8")
    with pytest.raises(ValueError, match="duplicate"):
        explicit_symbols(path)


def test_omission_file_rejects_unknown_symbols(tmp_path: Path) -> None:
    path = tmp_path / "unsupported.exports"
    path.write_text("arc4random\nmisspelled_random\n", encoding="utf-8")
    with pytest.raises(ValueError, match="misspelled_random"):
        omit_symbols(path, {"arc4random"})


def test_omission_file_returns_verified_symbols(tmp_path: Path) -> None:
    path = tmp_path / "unsupported.exports"
    path.write_text("# unavailable without entropy\narc4random\n",
                    encoding="utf-8")
    assert omit_symbols(path, {"arc4random", "puts"}) == {"arc4random"}


def test_writes_deterministic_contract_files(tmp_path: Path) -> None:
    version_map = tmp_path / "libc.map"
    response = tmp_path / "libc.rsp"
    symbols = {"write", "read"}

    write_version_map(version_map, "ASTRA_LIBC_1_0", symbols)
    write_undefined_response(response, symbols)

    assert version_map.read_text(encoding="utf-8") == (
        "ASTRA_LIBC_1_0 {\n"
        "    global:\n"
        "        read;\n"
        "        write;\n"
        "    local:\n"
        "        *;\n"
        "};\n"
    )
    assert response.read_text(encoding="utf-8") == (
        "--undefined=read\n--undefined=write\n"
    )
