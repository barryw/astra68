from pathlib import Path

from tools.check_shared_symbol_ownership import (
    definitions,
    duplicate_library_owners,
    duplicate_owners,
)


SYMBOLS = """
Symbol table '.symtab' contains 5 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     1: 00000000     8 FUNC    LOCAL  DEFAULT    1 hidden_copy
     2: 00000008     8 FUNC    GLOBAL DEFAULT    1 public_owner
     3: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND unresolved
     4: 00000000     0 FILE    LOCAL  DEFAULT  ABS source.c
"""


def test_full_definitions_include_localized_copies():
    assert definitions(SYMBOLS, public_only=False) == {
        "hidden_copy", "public_owner"
    }


def test_public_definitions_exclude_local_symbols():
    assert definitions(SYMBOLS, public_only=True) == {"public_owner"}


def test_duplicate_owners_are_sorted_and_allowable():
    assert duplicate_owners(
        {"second", "first", "private"}, {"first", "second"}, {"second"}
    ) == ["first"]


def test_duplicate_library_owners_check_every_direction():
    first = Path("first.library")
    second = Path("second.library")
    third = Path("third.library")
    assert duplicate_library_owners({
        first: ({"first_api", "second_api"}, {"first_api"}),
        second: ({"second_api"}, {"second_api"}),
        third: ({"third_api", "first_api"}, {"third_api"}),
    }, set()) == {
        first: ["second_api"],
        third: ["first_api"],
    }
