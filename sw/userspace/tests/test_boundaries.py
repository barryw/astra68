#!/usr/bin/env python3
"""Executable ownership rules for protected userspace."""

from pathlib import Path
import re


USERSPACE = Path(__file__).resolve().parents[1]
REPOSITORY = USERSPACE.parents[1]


def production_sources(root: Path):
    for path in root.rglob("*.c"):
        if "build" not in path.parts and "tests" not in path.parts:
            yield path


def relative(path: Path) -> str:
    return str(path.relative_to(REPOSITORY))


def require_absent(path: Path, patterns):
    text = path.read_text()
    for pattern, reason in patterns:
        if re.search(pattern, text, re.MULTILINE):
            raise AssertionError(f"{relative(path)}: {reason}")


def test_kernel_private_headers_do_not_cross_into_userspace():
    forbidden = re.compile(
        r'#\s*include\s*[<"](?:\.\./)*(?:sw/)?kernel/|'
        r'-I[^\n ]*kernel(?:/include)?(?:\s|$)'
    )
    roots = (USERSPACE, REPOSITORY / "ndk")
    for root in roots:
        for path in list(root.rglob("*.c")) + list(root.rglob("*.h")) + \
                    list(root.rglob("Makefile")):
            if "build" in path.parts:
                continue
            if forbidden.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: kernel-private dependency in userspace"
                )


def test_terminal_does_not_depend_on_supervisor_implementation():
    terminal = USERSPACE / "services" / "terminal"
    require_absent(
        terminal / "Makefile",
        (
            (r"\$\(SUPERVISOR\)/src/", "compiles supervisor-owned source"),
            (r"-I\$\(SUPERVISOR\)/include", "imports supervisor-private headers"),
        ),
    )
    for path in production_sources(terminal):
        require_absent(
            path,
            (
                (
                    r"#\s*include\s*[<\"](?:loader|vfs_host|volume)\.h[>\"]",
                    "imports a supervisor-private interface",
                ),
                (
                    r"^\s*(?:[A-Za-z_]\w*[\s*]+)+supervisor_[A-Za-z_]\w*\s*\(",
                    "implements a supervisor symbol inside Terminal",
                ),
            ),
        )


def test_filesystem_private_state_stays_in_vfs():
    private_fields = re.compile(
        r"\._private_(?:client|read_at|write_at|file|flags|offset|size|kind|member)\b|"
        r"->_private_(?:client|read_at|write_at|file|flags|offset|size|kind|member)\b"
    )
    for path in production_sources(USERSPACE):
        if USERSPACE / "vfs" in path.parents:
            continue
        if private_fields.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: reaches through filesystem.library private state"
            )


def test_service_startup_protocol_is_shared():
    services = ("events", "display", "storage", "input", "desktop", "terminal")
    for service in services:
        path = USERSPACE / "services" / service / "main.c"
        require_absent(
            path,
            (
                (
                    r"static\s+const\s+AstraStartupCapability\s*\*\s*"
                    r"capability\s*\(",
                    "duplicates runtime startup capability lookup",
                ),
                (
                    r"static\s+(?:void|uint32_t)\s+ready\s*\(",
                    "duplicates runtime service-ready publication",
                ),
                (
                    r"\.header\.protocol\s*=\s*ASTRA_SERVICE_PROTOCOL",
                    "serializes the shared service-ready protocol locally",
                ),
            ),
        )


def test_program_startup_capability_lookup_is_shared():
    for path in production_sources(USERSPACE / "commands"):
        require_absent(
            path,
            ((
                r"astra_capability_name_equal\s*\(",
                "parses startup capabilities instead of using runtime",
            ),),
        )
    for path in (
        USERSPACE / "posix" / "src" / "console.c",
        USERSPACE / "posix" / "src" / "system.c",
    ):
        require_absent(
            path,
            ((
                r"astra_capability_name_equal\s*\(",
                "parses startup capabilities instead of using runtime",
            ),),
        )
    require_absent(
        USERSPACE / "supervisor" / "src" / "main.c",
        ((
            r"static\s+const\s+AstraStartupCapability\s*\*\s*"
            r"find_capability\s*\(",
            "duplicates runtime startup capability lookup",
        ),),
    )
    require_absent(
        USERSPACE / "supervisor" / "src" / "loader.c",
        ((
            r"static\s+const\s+AstraStartupCapability\s*\*\s*"
            r"startup_capability\s*\(",
            "duplicates runtime startup capability lookup",
        ),),
    )


def test_native_commands_use_runtime_argument_access():
    for path in production_sources(USERSPACE / "commands"):
        if path.parent.name == "lua":
            continue
        require_absent(
            path,
            ((
                r"\bargv_address\b",
                "decodes the startup argument vector instead of using runtime",
            ),),
        )


def test_commands_use_shared_stream_writes():
    for path in production_sources(USERSPACE / "commands"):
        require_absent(
            path,
            ((
                r"static\s+int\s+write_all\s*\(",
                "wraps the shared all-or-error stream writer",
            ),),
        )
    require_absent(
        USERSPACE / "commands" / "which" / "which.c",
        (
            (r"static\s+void\s+say_number\s*\(",
             "duplicates the shared decimal stream writer"),
            (r"static\s+uint32_t\s+command_path\s*\(",
             "duplicates shared VFS path qualification"),
        ),
    )


def test_endian_primitives_are_shared():
    roots = (REPOSITORY / "sw" / "kernel", USERSPACE,
             REPOSITORY / "ndk")
    names = r"(?:get|put|read|write|load|store|take|index)_?be(?:16|32|64)|be(?:16|32|64)|put32"
    for root in roots:
        for path in production_sources(root):
            require_absent(
                path,
                ((
                    r"static\s+(?:inline\s+)?(?:u?int(?:16|32|64)_t|void)\s+"
                    + r"(?:" + names + r")\s*\(",
                    "duplicates the shared unaligned endian primitives",
                ),),
            )


def test_checked_integer_primitives_are_shared():
    forbidden = (
        r"static\s+(?:bool|int)\s+(?:is_)?power_of_two\s*\(",
        r"static\s+bool\s+add_checked\s*\(",
        r"static\s+(?:inline\s+)?(?:__attribute__\(\(always_inline\)\)\s+)?"
        r"void\s+(?:interrupt_)?increment_saturating\s*\(",
        r"static\s+uint64_t\s+slot_bit\s*\(",
    )
    roots = (REPOSITORY / "sw" / "kernel", USERSPACE,
             REPOSITORY / "ndk")
    for root in roots:
        for path in production_sources(root):
            require_absent(
                path,
                tuple((pattern, "duplicates shared checked integer logic")
                      for pattern in forbidden),
            )


def test_reserved_word_validation_is_shared():
    roots = (USERSPACE, REPOSITORY / "ndk")
    for root in roots:
        for path in production_sources(root):
            require_absent(
                path,
                ((r"static\s+int\s+words_(?:are_)?zero\s*\(",
                  "duplicates shared reserved-word validation"),),
            )


def test_standard_string_primitives_are_shared():
    roots = (USERSPACE, REPOSITORY / "ndk")
    forbidden = (
        r"static\s+(?:u?int32_t|size_t)\s+(?:shell_strlen|text_length)\s*\(",
        r"static\s+int\s+(?:equal|shell_equal)\s*\(\s*"
        r"const\s+char\s*\*\s*left\s*,\s*const\s+char\s*\*\s*right\s*\)",
        r"static\s+int\s+same\s*\([^)]*\)\s*\{\s*while\s*\(",
    )
    for root in roots:
        for path in production_sources(root):
            require_absent(
                path,
                tuple((pattern, "duplicates shared standard string logic")
                      for pattern in forbidden),
            )


def test_kernel_byte_comparison_is_shared():
    for path in production_sources(REPOSITORY / "sw" / "kernel"):
        if path.name == "bytes.c":
            continue
        require_absent(
            path,
            ((r"static\s+bool\s+bytes_equal\s*\(",
              "duplicates the shared kernel byte comparator"),),
        )


def test_analyzer_targets_do_real_work():
    roots = (USERSPACE / "commands", USERSPACE / "services")
    owners = (
        USERSPACE / "interface" / "Makefile",
        USERSPACE / "messaging" / "Makefile",
    )
    empty = re.compile(
        r"^[^\n:]*\banalyze\b[^\n:]*:\s*\n\s*@(?:true|:)\s*$",
        re.MULTILINE,
    )
    empty_sanitize = re.compile(
        r"^[^\n:]*\bsanitize\b[^\n:]*:\s*\n\s*@(?:true|:)\s*$",
        re.MULTILINE,
    )
    for root in roots:
        for path in root.rglob("Makefile"):
            if empty.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: analyzer target reports success "
                    "without analyzing code"
                )
    for path in owners:
        if empty.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: analyzer target reports success "
                "without analyzing code"
            )
        if empty_sanitize.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: sanitizer target reports success "
                "without testing code"
            )


def test_owner_analyzers_cover_production_sources():
    posix_makefile = (USERSPACE / "posix" / "Makefile").read_text()
    if not re.search(
        r"ANALYZER_OBJECTS\s*:=\s*\$\(patsubst\s+src/%\.c,"
        r"build/analyzer/%\.o,\$\(SOURCES\)\)",
        posix_makefile,
    ):
        raise AssertionError(
            "sw/userspace/posix/Makefile: analyzer does not cover every "
            "POSIX library source"
        )
    commands_makefile = (USERSPACE / "commands" / "Makefile").read_text()
    if "build/analyzer/lua-adapter.o" not in commands_makefile:
        raise AssertionError(
            "sw/userspace/commands/Makefile: analyzer omits the Astra-owned "
            "Lua adapter"
        )
    ndk_makefile = (REPOSITORY / "ndk" / "Makefile").read_text()
    if not re.search(
        r"ANALYZER_OBJECTS\s*:=\s*\$\(patsubst\s+src/%\.c,"
        r"build/analyzer/%\.o,\$\(SOURCES\)\)",
        ndk_makefile,
    ):
        raise AssertionError(
            "ndk/Makefile: analyzer and target do not share one source list"
        )
    supervisor_makefile = (USERSPACE / "supervisor" / "Makefile").read_text()
    for variable, directory in (
        ("OBJECTS", "build/m68k"),
        ("ANALYZER_OBJECTS", "build/analyzer"),
    ):
        if not re.search(
            rf"{variable}\s*:=\s*\$\(patsubst\s+src/%\.c,"
            rf"{directory}/%\.o,\$\(SOURCES\)\)",
            supervisor_makefile,
        ):
            raise AssertionError(
                "sw/userspace/supervisor/Makefile: target and analyzer do "
                "not share the complete Supervisor source list"
            )
    runtime_makefile = (USERSPACE / "runtime" / "Makefile").read_text()
    if not re.search(
        r"ANALYZER_OBJECTS\s*:=\s*\$\(patsubst\s+src/%\.c,"
        r"build/analyzer/%\.o,\$\(TARGET_C_SOURCES\)\)",
        runtime_makefile,
    ):
        raise AssertionError(
            "sw/userspace/runtime/Makefile: analyzer duplicates the runtime "
            "target source list"
        )
    storage_makefile = (USERSPACE / "storage" / "Makefile").read_text()
    if not re.search(
        r"ANALYZER_SOURCES\s*:=\s*\$\(SOURCES\).*\$\(PORT_SOURCES\)",
        storage_makefile,
    ) or not re.search(
        r"ANALYZER_OBJECTS\s*:=\s*\$\(patsubst\s+src/%\.c,"
        r"build/analyzer/%\.o,\$\(ANALYZER_SOURCES\)\)",
        storage_makefile,
    ):
        raise AssertionError(
            "sw/userspace/storage/Makefile: analyzer does not derive from "
            "every Astra-owned storage source list"
        )
    events_makefile = (USERSPACE / "events" / "Makefile").read_text()
    if not re.search(
        r"ANALYZER_SOURCES\s*:=\s*\$\(SOURCES\)\s+"
        r"src/events_library\.c",
        events_makefile,
    ) or "build/analyzer/common_crc32.o" not in events_makefile:
        raise AssertionError(
            "sw/userspace/events/Makefile: analyzer omits Events-owned "
            "library or common code"
        )


def test_loadable_libraries_use_owner_built_archives():
    checks = {
        USERSPACE / "vfs" / "Makefile": (
            r"build/m68k/library/runtime_",
            r"\$\(NDK\)/src/",
        ),
        USERSPACE / "graphics" / "Makefile": (
            r"build/m68k/library/runtime_",
            r"build/m68k/library/graphics_bundle",
        ),
        USERSPACE / "events" / "Makefile": (
            r"build/m68k/library/runtime_",
        ),
        USERSPACE / "interface" / "Makefile": (
            r"\$\((?:RUNTIME|GRAPHICS|NDK)\)/src/",
        ),
        USERSPACE / "messaging" / "Makefile": (
            r"\$\((?:RUNTIME|NDK)\)/src/",
        ),
    }
    for path, patterns in checks.items():
        require_absent(
            path,
            tuple((pattern, "recompiles implementation owned by another library")
                  for pattern in patterns),
        )


def test_static_archives_are_exact_replacements():
    shared = (REPOSITORY / "mk" / "m68k-cross.mk").read_text()
    for required in (
        "rm -f $@.tmp",
        "$(AR) rcs $@.tmp $^",
        "mv $@.tmp $@",
    ):
        if required not in shared:
            raise AssertionError(
                "mk/m68k-cross.mk: archive replacement is not atomic and exact"
            )
    makefiles = list(USERSPACE.rglob("Makefile")) + [
        REPOSITORY / "ndk" / "Makefile"
    ]
    direct_update = re.compile(
        r"^\s*(?:\$\(AR\)|(?:[\w./-]+-)?ar)\s+[^\n]*\br(?:c|s|q)",
        re.MULTILINE,
    )
    for path in makefiles:
        if direct_update.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: updates an archive without removing stale members"
            )


def test_ndk_private_headers_stay_in_ndk():
    forbidden = re.compile(r'#\s*include\s*[<"]internal/')
    for path in production_sources(USERSPACE):
        if forbidden.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: imports an NDK-private header"
            )


def test_public_headers_are_imported_by_namespace():
    roots = (REPOSITORY / "sw" / "kernel", USERSPACE)
    relative_public = re.compile(
        r'#\s*include\s*[<"](?:\.\./)+[^">]*include/astra/'
    )
    for root in roots:
        for path in list(root.rglob("*.c")) + list(root.rglob("*.h")):
            if "build" in path.parts or "tests" in path.parts:
                continue
            if relative_public.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: forwards or imports a public header "
                    "through another subsystem's directory"
                )


def test_gui_wire_types_are_owned_by_the_protocol():
    gui = (REPOSITORY / "sw" / "include" / "astra" / "gui.h").read_text()
    window = (REPOSITORY / "ndk" / "include" / "astra" /
              "window.h").read_text()
    if re.search(r'#\s*include\s*<astra/window\.h>', gui):
        raise AssertionError(
            "sw/include/astra/gui.h: system protocol depends on NDK client API"
        )
    if "typedef struct AstraWindowEvent" not in gui:
        raise AssertionError(
            "sw/include/astra/gui.h: GUI wire event is not protocol-owned"
        )
    if "typedef struct AstraWindowEvent" in window:
        raise AssertionError(
            "ndk/include/astra/window.h: duplicates a system protocol type"
        )


def test_message_header_serialization_is_shared():
    assignment = re.compile(r"(?:->|\.)header_size\s*=(?!=)")
    roots = (USERSPACE, REPOSITORY / "ndk")
    for root in roots:
        for path in production_sources(root):
            if assignment.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: serializes the shared message header locally"
                )


def test_ascii_case_primitives_are_shared():
    roots = (USERSPACE, REPOSITORY / "ndk")
    duplicate = re.compile(r"static\s+char\s+upper\s*\(")
    for root in roots:
        for path in production_sources(root):
            if duplicate.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: duplicates shared ASCII case conversion"
                )


def test_graphics_geometry_is_shared_within_graphics():
    duplicate = re.compile(r"static\s+uint32_t\s+rounded_inset\s*\(")
    for path in production_sources(USERSPACE / "graphics"):
        if duplicate.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: duplicates Graphics rounded geometry"
            )


def test_immutable_vfs_operations_are_shared():
    duplicate = re.compile(
        r"static\s+uint32_t\s+(?:proc|events)_"
        r"(?:write|sync|truncate|mkdir|unlink|rename|chmod|readlink)\s*\("
    )
    for root in (USERSPACE / "supervisor", USERSPACE / "events"):
        for path in production_sources(root):
            if duplicate.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: duplicates immutable VFS operations"
                )


def test_compiler_barrier_is_shared():
    duplicate = re.compile(
        r"static\s+(?:inline\s+)?void\s+"
        r"(?:trace_barrier|monitor_barrier|interrupt_barrier|"
        r"compiler_barrier|acquire_fence)\s*\("
    )
    roots = (REPOSITORY / "sw" / "kernel", REPOSITORY / "ndk")
    for root in roots:
        for path in production_sources(root):
            if duplicate.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: duplicates the shared compiler barrier"
                )


def test_runtime_diagnostics_and_time_are_shared():
    duplicate = re.compile(
        r"static\s+(?:void|uint32_t)\s+"
        r"(?:log_failure|launch_micros|elapsed_us)\s*\("
    )
    for path in production_sources(USERSPACE):
        if duplicate.search(path.read_text()):
            raise AssertionError(
                f"{relative(path)}: duplicates runtime diagnostics or time logic"
            )


def test_identical_device_callbacks_are_not_duplicated():
    kernel = (REPOSITORY / "sw" / "kernel" / "kernel.c").read_text()
    if re.search(r"static\s+bool\s+display_device_quiesce\s*\(", kernel):
        raise AssertionError(
            "sw/kernel/kernel.c: duplicates the display reset callback"
        )


def test_program_build_order_is_shared():
    makefiles = (
        USERSPACE / "commands" / "Makefile",
        USERSPACE / "supervisor" / "Makefile",
        *(USERSPACE / "services" / service / "Makefile" for service in (
            "events", "display", "storage", "desktop", "input", "terminal"
        )),
    )
    for path in makefiles:
        text = path.read_text()
        if "program.mk" not in text:
            raise AssertionError(
                f"{relative(path)}: duplicates or omits shared program build order"
            )
        if "ASTRA_PROGRAM_OWNER_DIRS" not in text:
            raise AssertionError(
                f"{relative(path)}: does not declare its library owners"
            )
        if re.search(r"^all\s*:", text, re.MULTILINE):
            raise AssertionError(
                f"{relative(path)}: defines program build order locally"
            )


def main():
    tests = (
        test_kernel_private_headers_do_not_cross_into_userspace,
        test_terminal_does_not_depend_on_supervisor_implementation,
        test_filesystem_private_state_stays_in_vfs,
        test_service_startup_protocol_is_shared,
        test_program_startup_capability_lookup_is_shared,
        test_native_commands_use_runtime_argument_access,
        test_commands_use_shared_stream_writes,
        test_endian_primitives_are_shared,
        test_checked_integer_primitives_are_shared,
        test_reserved_word_validation_is_shared,
        test_standard_string_primitives_are_shared,
        test_kernel_byte_comparison_is_shared,
        test_analyzer_targets_do_real_work,
        test_owner_analyzers_cover_production_sources,
        test_loadable_libraries_use_owner_built_archives,
        test_static_archives_are_exact_replacements,
        test_ndk_private_headers_stay_in_ndk,
        test_public_headers_are_imported_by_namespace,
        test_gui_wire_types_are_owned_by_the_protocol,
        test_message_header_serialization_is_shared,
        test_ascii_case_primitives_are_shared,
        test_graphics_geometry_is_shared_within_graphics,
        test_immutable_vfs_operations_are_shared,
        test_compiler_barrier_is_shared,
        test_runtime_diagnostics_and_time_are_shared,
        test_identical_device_callbacks_are_not_duplicated,
        test_program_build_order_is_shared,
    )
    failures = []
    for test in tests:
        try:
            test()
        except AssertionError as error:
            failures.append(str(error))
    if failures:
        raise SystemExit("\n".join(failures))
    print("ASTRA USERSPACE BOUNDARIES PASS")


if __name__ == "__main__":
    main()
