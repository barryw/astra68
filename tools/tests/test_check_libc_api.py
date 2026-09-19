from pathlib import Path

from tools.check_libc_api import aux_functions, public_headers, write_symbols


def test_aux_functions_keeps_only_installed_header_declarations(tmp_path: Path):
    include = tmp_path / "include"
    include.mkdir()
    text = "\n".join((
        f"/* {include}/stdio.h:10:NC */ extern int puts (const char *);",
        "/* /compiler/include/stddef.h:2:NC */ extern int internal (void);",
    ))
    assert aux_functions(text, include) == {"puts"}


def test_public_headers_skip_private_and_machine_headers(tmp_path: Path):
    include = tmp_path / "include"
    for name in ("stdio.h", "sys/stat.h", "sys/_types.h", "machine/setjmp.h",
                 "c++/16/vector.h"):
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    assert public_headers(include) == [Path("stdio.h"), Path("sys/stat.h")]


def test_aux_functions_keeps_compiler_abi_but_not_private_helpers(tmp_path: Path):
    include = tmp_path / "include"
    (include / "ssp").mkdir(parents=True)
    text = "\n".join((
        f"/* {include}/stdio.h:1:NC */ extern int __private (void);",
        f"/* {include}/ssp/string.h:2:NC */ extern int __memcpy_chk (void);",
    ))
    assert aux_functions(text, include) == {"__memcpy_chk"}


def test_aux_functions_handles_function_pointer_parameters(tmp_path: Path):
    include = tmp_path / "include"
    include.mkdir()
    text = (f"/* {include}/search.h:81:NC */ extern void twalk "
            "(const void *, void (*) (const void *, int));")
    assert aux_functions(text, include) == {"twalk"}


def test_aux_functions_handles_function_pointer_returns(tmp_path: Path):
    include = tmp_path / "include"
    include.mkdir()
    text = (f"/* {include}/signal.h:20:NC */ extern void (*signal "
            "(int, void (*) (int))) (int);")
    assert aux_functions(text, include) == {"signal"}


def test_write_symbols_is_sorted_atomic_and_stable(tmp_path: Path):
    output = tmp_path / "nested" / "symbols.txt"
    write_symbols(output, {"zeta", "alpha"})
    original_mtime = output.stat().st_mtime_ns

    write_symbols(output, {"alpha", "zeta"})

    assert output.read_text() == "alpha\nzeta\n"
    assert output.stat().st_mtime_ns == original_mtime
    assert not output.with_suffix(".txt.tmp").exists()
