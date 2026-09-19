from pathlib import Path

import pytest

from tools.check_library_exports import exported_symbols


def test_reads_explicit_global_exports(tmp_path: Path) -> None:
    version_map = tmp_path / "library.map"
    version_map.write_text(
        "ABI_1 { global: astra_one; astra_two; local: *; };\n",
        encoding="utf-8",
    )
    assert exported_symbols(version_map) == ["astra_one", "astra_two"]


def test_reads_exports_from_every_version_node(tmp_path: Path) -> None:
    version_map = tmp_path / "library.map"
    version_map.write_text(
        "ABI_1.0 { global: astra_one; local: *; };\n"
        "ABI_1.1 { global: astra_two; } ABI_1.0;\n",
        encoding="utf-8",
    )
    assert exported_symbols(version_map) == ["astra_one", "astra_two"]


def test_rejects_wildcard_export(tmp_path: Path) -> None:
    version_map = tmp_path / "library.map"
    version_map.write_text(
        "ABI_1 { global: astra_*; local: *; };\n", encoding="utf-8"
    )
    with pytest.raises(ValueError, match="explicit C identifier"):
        exported_symbols(version_map)


def test_rejects_duplicate_export(tmp_path: Path) -> None:
    version_map = tmp_path / "library.map"
    version_map.write_text(
        "ABI_1 { global: astra_one; astra_one; local: *; };\n",
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="duplicate exports"):
        exported_symbols(version_map)
