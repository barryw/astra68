from pathlib import Path

import pytest

from tools.generate_header_exports import declared_functions, main


def test_finds_multiline_macro_marked_functions_only() -> None:
    source = """
        API int (one) (void);
        API const char *
        (two) (int value,
               const char *text);
        INTERNAL void (hidden) (void);
        /* API int (commented) (void); */
    """
    assert declared_functions(source, ["API"]) == {"one", "two"}


def test_rejects_an_empty_macro_list() -> None:
    with pytest.raises(ValueError, match="at least one"):
        declared_functions("API int (one) (void);", [])
