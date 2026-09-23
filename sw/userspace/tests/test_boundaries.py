#!/usr/bin/env python3
"""Executable ownership rules for protected userspace."""

from pathlib import Path
import re
import subprocess
import tempfile


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


def require_vfs_mutex_contract(source: str):
    if re.search(r"astra_rt_semaphore_create\(\s*1u,\s*1u", source):
        raise AssertionError("VFS lock is still a syscall semaphore")
    if not re.search(
        r"astra_vfs_service_set_state_lock\(.*?"
        r"astra_vfs_state_lock_acquire\s*,\s*"
        r"astra_vfs_state_lock_release",
        source,
        re.DOTALL,
    ):
        raise AssertionError("VFS state wait requires mutex callbacks")


def require_aligned_service_locks(source: str, names):
    for name in names:
        if not re.search(
            rf"static\s+_Alignas\(4\)\s+uint32_t\s+{name}\s*;", source
        ):
            raise AssertionError(f"{name} is not futex-aligned")


def test_vfs_services_use_the_mutex_expected_by_state_wait():
    locks = {
        "storage": ("state_lock", "mount_lock", "mount_reader_lock",
                    "cache_lock", "fill_lock", "backend_table_lock",
                    "backend_scan_lock"),
        "hostfs": ("transport_lock", "state_lock"),
    }
    for name, words in locks.items():
        source = (USERSPACE / "services" / name / "main.c").read_text()
        require_vfs_mutex_contract(source)
        require_aligned_service_locks(source, words)
        assert "astra_vfs_state_futex_wait" in source
    storage = (USERSPACE / "services" / "storage" / "main.c").read_text()
    assert "astra_mutex_lock(&cache_lock)" in storage
    assert "astra_mutex_unlock(&cache_lock)" in storage

    # The previous semaphore callback and semaphore creation must both fail.
    for invalid in (
        storage.replace("astra_vfs_state_lock_acquire",
                        "vfs_state_acquire"),
        storage + "\nastra_rt_semaphore_create(1u, 1u, 0u, &state_lock);\n",
    ):
        try:
            require_vfs_mutex_contract(invalid)
        except AssertionError:
            pass
        else:
            raise AssertionError("VFS mutex regression was accepted")
    try:
        require_aligned_service_locks(
            storage.replace("_Alignas(4) uint32_t mount_lock",
                            "uint32_t mount_lock"), locks["storage"]
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("misaligned storage mutex was accepted")


def require_storage_boot_gate(source: str):
    if not re.search(
        r"if\s*\(block_device\s*==\s*0u\)\s*"
        r"return\s*\(int\)\(ASTRA_SUPERVISOR_STATUS_TAG\s*\|\s*"
        r"ASTRA_SUPERVISOR_FAIL_BLOCK_LEASE\)", source
    ):
        raise AssertionError("boot must fail when no storage lease exists")
    if re.search(r"\bpark\s*\(", source):
        raise AssertionError("missing storage must not park silently")


def test_missing_storage_stops_normal_boot():
    source = (USERSPACE / "supervisor" / "src" / "main.c").read_text()
    require_storage_boot_gate(source)
    try:
        require_storage_boot_gate(source.replace(
            "ASTRA_SUPERVISOR_FAIL_BLOCK_LEASE", "ASTRA_STATUS_OK"
        ))
    except AssertionError:
        pass
    else:
        raise AssertionError("storage-free boot was accepted")


def require_storage_service_boot_gate(source: str):
    startup = source.split("uint32_t supervisor_loader_start(", 1)[1]
    storage = startup.split("supervisor_bootstrap_block_close();", 1)[0]
    gate = "if (status != ASTRA_STATUS_OK)\n        return status;\n    supervisor_bootstrap_block_close();"

    assert "launch_entry(startup, &manifest->entries[0]" in storage
    assert gate in startup


def test_failed_storage_service_stops_normal_boot():
    source = (USERSPACE / "supervisor" / "src" / "loader.c").read_text()
    require_storage_service_boot_gate(source)
    broken = source.replace(
        "if (status != ASTRA_STATUS_OK)\n        return status;\n    supervisor_bootstrap_block_close();",
        "supervisor_bootstrap_block_close();",
    )
    try:
        require_storage_service_boot_gate(broken)
    except AssertionError:
        pass
    else:
        raise AssertionError("storage launch failure was allowed to continue")


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


def test_posix_command_uses_the_vfs_path_authority():
    path = USERSPACE / "commands" / "posix" / "posix.c"
    text = path.read_text()

    for name in ("before", "after"):
        if not re.search(
            rf"char\s+{name}\s*\[ASTRA_VFS_PATH_MAX\]", text
        ):
            raise AssertionError(
                f"{relative(path)}: {name} has a private current-directory "
                "limit instead of the VFS path authority"
            )
    if re.search(r"char\s+(?:before|after)\s*\[[0-9]+\]", text):
        raise AssertionError(
            f"{relative(path)}: numeric current-directory limit returned"
        )


def test_commands_do_not_restore_private_formatting_caps():
    posix = USERSPACE / "commands" / "posix" / "posix.c"
    devices = USERSPACE / "commands" / "devices" / "devices.c"

    require_absent(
        posix,
        ((r"char\s+report\s*\[[^]]+\]",
          "clips failure diagnostics into a private report buffer"),),
    )
    if posix.read_text().count("(void)astra_log(reason);") != 1:
        raise AssertionError(
            f"{relative(posix)}: complete resolver failure reason is not logged"
        )
    require_absent(
        devices,
        ((r"char\s+(?:digits|text)\s*\[[^]]+\]",
          "formats integers through a private numeric ceiling"),),
    )
    if 'printf("%*u"' not in devices.read_text():
        raise AssertionError(
            f"{relative(devices)}: libc does not own numeric field formatting"
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
    owners = (USERSPACE / "interface" / "Makefile",)
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
    if ("ANALYZER_SOURCES := $(SOURCES)" not in events_makefile or
            "build/analyzer/common_crc32.o" not in events_makefile):
        raise AssertionError(
            "sw/userspace/events/Makefile: analyzer omits Events-owned code"
        )


def test_loadable_libraries_use_owner_built_archives():
    checks = {
        USERSPACE / "vfs" / "Makefile": (
            r"build/m68k/library/runtime_",
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
    }
    for path, patterns in checks.items():
        require_absent(
            path,
            tuple((pattern, "recompiles implementation owned by another library")
                  for pattern in patterns),
        )

    vfs_makefile = (USERSPACE / "vfs" / "Makefile").read_text()
    filesystem_objects = re.search(
        r"^FILESYSTEM_LIBRARY_OBJECTS\s*:=.*?(?=^\S|\Z)",
        vfs_makefile,
        re.MULTILINE | re.DOTALL,
    )
    if filesystem_objects is None:
        raise AssertionError(
            "sw/userspace/vfs/Makefile: missing filesystem library object set"
        )
    if re.search(r"(?:\$\(NDK\)/src/|/ndk_|runtime_)", filesystem_objects.group()):
        raise AssertionError(
            "sw/userspace/vfs/Makefile: filesystem library recompiles "
            "implementation owned by another library"
        )


def test_shared_library_products_have_one_registry():
    registry = USERSPACE / "libraries.mk"
    if not registry.is_file():
        raise AssertionError(
            "sw/userspace/libraries.mk: missing canonical shared-library "
            "product registry"
        )
    registry_text = registry.read_text()
    required = (
        "ASTRA_SYSTEM_LIBRARY",
        "ASTRA_COMPILER_LIBRARY",
        "ASTRA_RUNTIME_LIBRARY",
        "ASTRA_FILESYSTEM_LIBRARY",
        "ASTRA_LIBC_LIBRARY",
        "ASTRA_CXX_LIBRARY",
        "ASTRA_NETWORK_LIBRARY",
        "ASTRA_SHARED_LIBRARIES",
        "ASTRA_SHARED_LIBRARY_SEARCH_FLAGS",
    )
    for variable in required:
        if not re.search(rf"^{variable}\s*:?=", registry_text, re.MULTILINE):
            raise AssertionError(
                f"sw/userspace/libraries.mk: missing {variable}"
            )

    consumers = (
        USERSPACE / "Makefile",
        USERSPACE / "kits" / "Makefile",
        USERSPACE / "commands" / "Makefile",
    )
    for path in consumers:
        text = path.read_text()
        if "libraries.mk" not in text:
            raise AssertionError(
                f"{relative(path)}: bypasses the shared-library registry"
            )
    kits = (USERSPACE / "kits" / "Makefile").read_text()
    if re.search(r"build/m68k/libraries/[^\s]+\.library", kits):
        raise AssertionError(
            "sw/userspace/kits/Makefile: duplicates a library product path"
        )
    if "OWNER_DIRS" in kits or re.search(r"\|\s*owners\b", kits):
        raise AssertionError(
            "sw/userspace/kits/Makefile: a Kit build walks unrelated library "
            "owners"
        )
    if not re.search(
        r"^\$\(NETWORK_LIBRARY\):\s+FORCE\s*$.*?"
        r"\$\(MAKE\)\s+-C\s+\.\./network\s+library-contract",
        kits,
        re.MULTILINE | re.DOTALL,
    ):
        raise AssertionError(
            "sw/userspace/kits/Makefile: Network Kit lacks an exact owner "
            "dependency"
        )


def test_library_contracts_match_exact_readelf_fields():
    makefiles = tuple(USERSPACE.rglob("Makefile")) + (
        REPOSITORY / "ndk" / "Makefile",
    )
    for path in makefiles:
        text = path.read_text()
        if "NEEDED.*" in text or "SONAME.*" in text:
            raise AssertionError(
                f"{relative(path)}: fuzzy readelf match can confuse "
                "overlapping library names"
            )

    config = (USERSPACE / "config" / "Makefile").read_text()
    required = (
        "grep -Fc 'Shared library: [system.library.2]'",
        "grep -Fc 'Shared library: [filesystem.library.3]'",
    )
    for contract in required:
        if contract not in config:
            raise AssertionError(
                "sw/userspace/config/Makefile: missing exact dependency "
                f"contract {contract}"
            )


def test_link_products_depend_on_their_build_contracts():
    policy = (REPOSITORY / "mk" / "astra-shared-library.mk").read_text()
    for token in (
        "ASTRA_SHARED_OWNER_MAKEFILE",
        "ASTRA_SHARED_POLICY_MAKEFILE",
        "ASTRA_SHARED_LINK_INPUTS",
    ):
        if token not in policy:
            raise AssertionError(
                "mk/astra-shared-library.mk: shared objects can survive a "
                f"changed link contract ({token})"
            )

    owners = [REPOSITORY / "ndk" / "Makefile"]
    owners.extend(USERSPACE.rglob("Makefile"))
    for path in owners:
        text = path.read_text()
        if "astra-shared-library.mk" not in text:
            continue
        if "ASTRA_SHARED_LINK_INPUTS" not in text:
            raise AssertionError(
                f"{relative(path)}: shared-library target omits the common "
                "link-input contract"
            )
        if "$(ASTRA_SHARED_LIBRARY_LD)" in text:
            raise AssertionError(
                f"{relative(path)}: shared-library target bypasses owner "
                "Makefile freshness"
            )

    dynamic = (USERSPACE / "dynamic-program.mk").read_text()
    for dependency in (
        "Makefile",
        "$(ASTRA_USERSPACE_ROOT)/dynamic-program.mk",
        "$(ASTRA_USERSPACE_ROOT)/program.mk",
        "$(ASTRA_USERSPACE_ROOT)/libraries.mk",
    ):
        if dependency not in dynamic:
            raise AssertionError(
                "sw/userspace/dynamic-program.mk: executable can survive a "
                f"changed link contract ({dependency})"
            )
    definition = dynamic.find("$(ASTRA_PROGRAM_LINK_SCRIPT_FLAGS)")
    script = dynamic.find("$(ASTRA_DYNAMIC_LINK_SCRIPT_FLAGS)")
    if definition < 0 or script < 0 or definition > script:
        raise AssertionError(
            "sw/userspace/dynamic-program.mk: program-specific linker "
            "definitions do not precede the canonical linker script"
        )
    terminal = (USERSPACE / "services" / "terminal" / "Makefile").read_text()
    if "ASTRA_PROGRAM_LINK_SCRIPT_FLAGS := -T $(EVENT_BASE_LD)" not in terminal:
        raise AssertionError(
            "sw/userspace/services/terminal/Makefile: event catalog base does "
            "not use the canonical pre-script definition contract"
        )
    if "ASTRA_PROGRAM_EXTRA_LINK_FLAGS" in dynamic or \
            "ASTRA_PROGRAM_EXTRA_LINK_FLAGS" in terminal:
        raise AssertionError(
            "dynamic program link retains the order-unsafe post-script hook"
        )


def test_services_do_not_compile_private_ndk_or_config_copies():
    for relative_makefile in (
        ("input", "Makefile"),
        ("terminal", "Makefile"),
        ("services", "terminal", "Makefile"),
    ):
        path = USERSPACE.joinpath(*relative_makefile)
        require_absent(
            path,
            (
                (r"build/m68k/ndk_keymap\.o",
                 "compiles a private copy of the NDK keymap"),
                (r"\$\(CONFIG\)/src/config_document\.c",
                 "compiles a private copy of the configuration parser"),
            ),
        )
    input_service = (USERSPACE / "services" / "input" / "Makefile").read_text()
    if "$(ASTRA_SYSTEM_LIBRARY)" not in input_service or \
            "libastra.a" in input_service:
        raise AssertionError(
            "sw/userspace/services/input/Makefile: does not consume the "
            "canonical dynamic system library"
        )


def test_cxx_command_uses_the_shared_runtime():
    commands = (USERSPACE / "commands" / "Makefile").read_text()
    if "CXX_IMAGE" in commands:
        raise AssertionError(
            "sw/userspace/commands/Makefile: packages the static C++ link "
            "contract as a command"
        )
    for token in (
        "cxx_LIBRARIES :=",
        "$(CXX_LIBRARY)",
        "$(UNWIND_LIBRARY)",
        "$(ASTRA_DYNAMIC_EXECUTABLE_CHECK)",
    ):
        if token not in commands:
            raise AssertionError(
                "sw/userspace/commands/Makefile: C++ command is not an "
                f"audited dynamic executable ({token})"
            )


def test_ntpd_uses_a_required_dynamic_config_dependency():
    makefile = (USERSPACE / "services" / "ntpd" / "Makefile").read_text()
    source = (USERSPACE / "services" / "ntpd" / "main.c").read_text()
    header = (REPOSITORY / "ndk" / "include" / "astra" /
              "config_library.h").read_text()

    for token in (
        "dynamic-program.mk",
        "$(CONFIG_LIBRARY)",
    ):
        if token not in makefile:
            raise AssertionError(
                "sw/userspace/services/ntpd/Makefile: required config "
                f"dependency is not an audited dynamic link ({token})"
            )
    if "-static" in makefile:
        raise AssertionError(
            "sw/userspace/services/ntpd/Makefile: required libraries are "
            "copied into ntpd"
        )
    if "OpenLibrary(" in source or "CloseLibrary(" in source:
        raise AssertionError(
            "sw/userspace/services/ntpd/main.c: required config dependency "
            "is discovered after initial-exec TLS is committed"
        )
    if ("astra_config_open(" not in header or
            "AstraConfigLibraryV1" in header or
            "astra_config_library(void)" in header):
        raise AssertionError(
            "ndk/include/astra/config_library.h: linked clients lack the "
            "canonical direct-symbol Config Kit API"
        )


def test_native_gui_programs_use_the_eager_dynamic_loader():
    programs = (
        USERSPACE / "services" / "terminal" / "Makefile",
        USERSPACE / "services" / "desktop" / "Makefile",
        USERSPACE / "applications" / "interface-gallery" / "Makefile",
    )
    required = (
        "dynamic-program.mk",
        "$(ASTRA_INTERFACE_LIBRARY)",
        "$(ASTRA_SYSTEM_LIBRARY)",
    )
    forbidden = ("-static", "libastrart.a", "libastra.a", "libastravfs.a")
    for path in programs:
        text = path.read_text()
        for token in required:
            if token not in text:
                raise AssertionError(
                    f"{relative(path)}: native GUI program bypasses the "
                    f"canonical eager dynamic link ({token})"
                )
        for token in forbidden:
            if token in text:
                raise AssertionError(
                    f"{relative(path)}: ordinary program retains a static "
                    f"copy of shared system code ({token})"
                )


def test_ordinary_native_programs_share_the_dynamic_link_contract():
    bootstrap_exceptions = {
        USERSPACE / "services" / "storage" / "Makefile",
    }
    service_makefiles = set((USERSPACE / "services").glob("*/Makefile"))
    application_makefiles = set(
        (USERSPACE / "applications").glob("*/Makefile")
    )
    programs = sorted(
        (service_makefiles - bootstrap_exceptions) | application_makefiles
    )
    if not programs or not bootstrap_exceptions.issubset(service_makefiles):
        raise AssertionError(
            "ordinary native program discovery lost a production Makefile"
        )
    forbidden = (
        "-static",
        "libastrart.a",
        "libastra.a",
        "libastravfs.a",
        "libastragraphics.a",
        "libastranetwork.a",
        "crt0.o",
    )
    for path in programs:
        text = path.read_text()
        if "dynamic-program.mk" not in text:
            raise AssertionError(
                f"{relative(path)}: bypasses the one native dynamic-program "
                "link contract"
            )
        for token in forbidden:
            if token in text:
                raise AssertionError(
                    f"{relative(path)}: ordinary program copies shared "
                    f"system code ({token})"
                )

    bootstrap_images = bootstrap_exceptions | {
        USERSPACE / "supervisor" / "Makefile",
    }
    for path in bootstrap_images:
        text = path.read_text()
        if "-static" not in text or "ASTRA_STATIC_LINK_SCRIPT_FLAGS" not in text:
            raise AssertionError(
                f"{relative(path)}: bootstrap image is not an explicit "
                "static-link contract"
            )


def test_shared_libraries_have_one_loader_and_one_symbol_abi():
    removed = (
        USERSPACE / "runtime" / "src" / "library.c",
        USERSPACE / "runtime" / "include" / "astra" /
        "library_loader.h",
        REPOSITORY / "ndk" / "include" / "astra" / "shared_library.h",
        REPOSITORY / "ndk" / "include" / "astra" / "events_library.h",
        REPOSITORY / "ndk" / "include" / "astra" / "input_library.h",
        REPOSITORY / "ndk" / "include" / "astra" / "messaging_library.h",
        USERSPACE / "events" / "src" / "events_library.c",
        USERSPACE / "interface" / "src" / "input_library.c",
        USERSPACE / "messaging" / "Makefile",
    )
    for path in removed:
        if path.exists():
            raise AssertionError(
                f"{relative(path)}: duplicate runtime library loader remains"
            )

    forbidden = re.compile(
        r"\b(?:OpenLibrary|CloseLibrary|astra_library_cleanup)\s*\(|"
        r"\b[a-zA-Z0-9_]+_library_(?:open|close)\s*\(|"
        r"\bASTRA_LIBRARY_EXPORTS\b|\bASTRA_LIBRARY\s*\("
    )
    roots = (USERSPACE, REPOSITORY / "ndk")
    for root in roots:
        for path in list(root.rglob("*.c")) + list(root.rglob("*.h")):
            if "build" in path.parts or "tests" in path.parts:
                continue
            if forbidden.search(path.read_text()):
                raise AssertionError(
                    f"{relative(path)}: bypasses the one eager ELF loader or "
                    "retains the displaced export-table ABI"
                )


def test_shared_libraries_have_one_provider_resolver():
    reader_header = USERSPACE / "vfs" / "include" / "astra" / "vfs_reader.h"
    reader_source = USERSPACE / "vfs" / "src" / "vfs_reader.c"
    consumers = (
        USERSPACE / "vfs" / "src" / "vfs_process.c",
        USERSPACE / "supervisor" / "src" / "loader.c",
        USERSPACE / "loader" / "main.c",
    )
    if "astra_vfs_library_source_open" not in reader_header.read_text() or \
            "astra_vfs_library_source_open" not in reader_source.read_text():
        raise AssertionError(
            "sw/userspace/vfs: canonical provider resolver is missing"
        )
    for path in consumers:
        text = path.read_text()
        if "astra_vfs_provider_index_parse" in text or \
                ":.providers/" in text:
            raise AssertionError(
                f"{relative(path)}: duplicates provider-index resolution"
            )
    if "astra_vfs_library_source_open" not in \
            (USERSPACE / "vfs" / "src" / "vfs_process.c").read_text() or \
            "astra_vfs_library_source_open" not in \
            (USERSPACE / "supervisor" / "src" / "loader.c").read_text():
        raise AssertionError(
            "process and bootstrap loading do not share the canonical "
            "provider resolver"
        )


def test_terminal_programs_share_terminfo():
    registry = (USERSPACE / "libraries.mk").read_text()
    if "ASTRA_TERMINFO_LIBRARY" not in registry:
        raise AssertionError(
            "sw/userspace/libraries.mk: missing the shared terminfo product"
        )
    if not (USERSPACE / "terminfo" / "Makefile").is_file():
        raise AssertionError(
            "sw/userspace/terminfo: missing canonical terminfo library owner"
        )
    manifest = (USERSPACE / "kits" / "Posix.kit" / "manifest").read_text()
    if "provides terminfo.library 6 6.6.0" not in manifest:
        raise AssertionError(
            "sw/userspace/kits/Posix.kit: does not publish terminfo.library"
        )
    commands = (USERSPACE / "commands" / "Makefile").read_text()
    if re.search(r"(?:VIM|ZSH)[^\n]*LDFLAGS[^\n]*-static", commands):
        raise AssertionError(
            "sw/userspace/commands/Makefile: Vim or Zsh is statically linked"
        )
    for variable in ("vim_LIBRARIES", "zsh_LIBRARIES"):
        declaration = re.search(
            rf"^{variable}\s*:=.*?(?=^\S|\Z)",
            commands,
            re.MULTILINE | re.DOTALL,
        )
        if declaration is None or "$(TERMINFO_LIBRARY)" not in declaration.group():
            raise AssertionError(
                f"sw/userspace/commands/Makefile: {variable} does not use "
                "terminfo.library"
            )


def test_static_archives_are_exact_replacements():
    shared = (REPOSITORY / "mk" / "m68k-cross.mk").read_text()
    for required in (
        "rm -f $@.tmp",
        "$(AR) rcs $@.tmp $(filter %.o,$^)",
        "mv $@.tmp $@",
    ):
        if required not in shared:
            raise AssertionError(
                "mk/m68k-cross.mk: archive replacement is not atomic and exact"
            )
    if "$(AR) rcs $@.tmp $^" in shared:
        raise AssertionError(
            "mk/m68k-cross.mk: archive replacement admits non-object prerequisites"
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


def test_posix_exec_uses_the_posix_namespace_converter():
    path = USERSPACE / "posix" / "src" / "file.c"
    text = path.read_text()
    start = text.index("\nexecve(const char *path")
    end = text.index("\nint\nfsync(", start)
    body = text[start:end]

    if "access(path, X_OK)" not in body or \
            "resolve(path, &resolved)" not in body:
        raise AssertionError(
            f"{relative(path)}: execve bypasses executable or slash-path checks"
        )
    if "astra_process_path(" in body:
        raise AssertionError(
            f"{relative(path)}: execve treats POSIX slash paths as native-relative"
        )


def test_posix_dependencies_have_one_recursive_build_barrier():
    text = (USERSPACE / "posix" / "Makefile").read_text()
    dynamic_targets = {
        "RUNTIME": "library",
        "STREAMS": "library",
        "VFS": "library",
        "NETWORK": "library",
    }
    static_targets = {
        "RUNTIME": "build/m68k/libastrart.a",
        "STREAMS": "build/m68k/libastrastreams.a",
        "VFS": "build/m68k/libastravfs.a",
        "NETWORK": "build/m68k/libastranetwork.a",
    }
    for owner, target in dynamic_targets.items():
        invocation = f"$(MAKE) -C $({owner}) {target}"
        if text.count(invocation) != 1:
            raise AssertionError(
                "sw/userspace/posix/Makefile: dependency owner can be built "
                f"concurrently outside the dynamic barrier ({owner})"
            )
    for owner, target in static_targets.items():
        invocation = f"$(MAKE) -C $({owner}) {target}"
        if text.count(invocation) != 1:
            raise AssertionError(
                "sw/userspace/posix/Makefile: dependency owner can be built "
                f"concurrently outside the static barrier ({owner})"
            )
    for artifact in ("RUNTIME_LIB", "RUNTIME_CRT0", "ASTRA_POSIX_CRT0",
                     "STREAMS_LIB", "VFS_LIB", "NETWORK_LIB"):
        if not re.search(
            rf"^.*\$\({artifact}\).*:\s*\|\s*static-libraries-ready\s*$",
            text,
            re.MULTILINE,
        ):
            raise AssertionError(
                "sw/userspace/posix/Makefile: dependency artifact bypasses "
                f"the single recursive build barrier ({artifact})"
            )

    vfs = (USERSPACE / "vfs" / "Makefile").read_text()
    if vfs.count("$(MAKE) -C $(RUNTIME)") != 1 or \
            vfs.count("$(MAKE) -C $(NDK) build/m68k/manifest.o") != 1:
        raise AssertionError(
            "sw/userspace/vfs/Makefile: loadable-library dependencies can be "
            "built concurrently through multiple recursive makes"
        )
    if not re.search(
        r"^\$\(NDK_OBJECTS\):\s*\|\s*libraries-ready\s*$", vfs, re.MULTILINE
    ) or not re.search(
        r"^\$\(FILESYSTEM_LIBRARY\):.*?\|\s*libraries-ready\s*$",
        vfs,
        re.MULTILINE | re.DOTALL,
    ):
        raise AssertionError(
            "sw/userspace/vfs/Makefile: NDK artifacts bypass the single "
            "recursive build barrier"
        )


def test_graphics_library_objects_track_all_inputs():
    path = USERSPACE / "graphics" / "Makefile"
    text = path.read_text()
    for target in (
        "build/m68k/library/graphics_library.o",
        "build/m68k/library/font_library.o",
        "build/m68k/library/graphics_surface.o",
        "build/m68k/library/font_surface.o",
        "build/m68k/library/graphics_shared_surface.o",
    ):
        match = re.search(
            rf"^{re.escape(target)}:.*?(?=^\S|\Z)",
            text,
            re.MULTILINE | re.DOTALL,
        )
        if match is None or "$(DEPFLAGS)" not in match.group(0):
            raise AssertionError(
                f"sw/userspace/graphics/Makefile: {target} can remain stale "
                "after an included header changes"
            )
    if "$(GRAPHICS_LIBRARY_OBJECTS) $(FONT_LIBRARY_OBJECTS): Makefile" not in text:
        raise AssertionError(
            "sw/userspace/graphics/Makefile: library objects do not rebuild "
            "when their dependency recipe changes"
        )
    if "build/m68k/*/*.d" not in text:
        raise AssertionError(
            "sw/userspace/graphics/Makefile: library dependency files are ignored"
        )


def test_interface_library_objects_track_all_inputs():
    path = USERSPACE / "interface" / "Makefile"
    text = path.read_text()
    for target in (
        "build/m68k/interface_library.o",
        "build/m68k/control.o",
        "build/m68k/text_surface.o",
        "build/m68k/undo.o",
    ):
        match = re.search(
            rf"^{re.escape(target)}:.*?(?=^\S|\Z)",
            text,
            re.MULTILINE | re.DOTALL,
        )
        if match is None or "$(DEPFLAGS)" not in match.group(0):
            raise AssertionError(
                f"sw/userspace/interface/Makefile: {target} can remain stale "
                "after an included header changes"
            )
    if "$(INTERFACE_OBJECTS): Makefile" not in text:
        raise AssertionError(
            "sw/userspace/interface/Makefile: library objects do not rebuild "
            "when their dependency recipe changes"
        )
    if "build/m68k/*.d" not in text:
        raise AssertionError(
            "sw/userspace/interface/Makefile: library dependency files are ignored"
        )


def test_supervisor_owns_local_service_endpoint_lifetimes():
    source = (USERSPACE / "supervisor" / "src" / "loader.c").read_text()
    start = source.index("uint32_t supervisor_loader_start(")
    end = source.index("uint32_t supervisor_loader_process_handle(", start)
    startup = source[start:end]

    for handle in ("event_target_send", "launch_send"):
        if f"astra_close({handle})" in startup or f"{handle} = 0u" in startup:
            raise AssertionError(
                f"supervisor closes its provider-owned {handle} after boot"
            )


def test_service_definitions_use_inline_vfs_writes():
    source = (USERSPACE / "supervisor" / "src" / "loader.c").read_text()
    start = source.index("static uint32_t dynamic_definition_write(")
    end = source.index("static uint32_t startup_definition(", start)
    writer = source[start:end]

    if "astra_vfs_write(" not in writer:
        raise AssertionError(
            "dynamic service definitions do not use the ordinary VFS write path"
        )
    if "astra_vfs_port_write_bulk(" in writer:
        raise AssertionError(
            "dynamic service definitions allocate a bulk transfer area for a "
            "small control record"
        )


def test_userspace_orchestration_does_not_overlap_shared_producers():
    text = (USERSPACE / "Makefile").read_text()

    if not re.search(r"^\.NOTPARALLEL:\s*$", text, re.MULTILINE):
        raise AssertionError(
            "sw/userspace/Makefile: parallel top-level goals can concurrently "
            "configure or replace the same shared producer"
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


def test_ndk_owns_user_facing_headers():
    public = (
        "event_catalog.h",
        "filesystem_library.h",
        "font_library.h",
        "graphics_library.h",
        "keymap.h",
        "posix.h",
        "runtime.h",
        "stream.h",
        "surface.h",
        "terminal.h",
        "text_style.h",
        "text_surface.h",
        "vfs_assign.h",
        "vfs_client.h",
        "vfs_path.h",
        "vfs_process.h",
        "vfs_union.h",
    )
    ndk = REPOSITORY / "ndk" / "include" / "astra"

    for name in public:
        implementation_copies = tuple(
            USERSPACE.glob(f"*/include/astra/{name}")
        )
        if not (ndk / name).is_file() or implementation_copies:
            raise AssertionError(
                f"{name}: user-facing contract must exist only in "
                f"ndk/include/astra; implementation copies="
                f"{[relative(path) for path in implementation_copies]}"
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
    shared = (USERSPACE / "program.mk").read_text()
    if ("ASTRA_PROGRAM_DIRECT_TARGETS ?= $(ASTRA_PROGRAM_TARGETS)" not in shared or
            "ASTRA_PROGRAM_DIRECT_GOALS" not in shared or
            "ASTRA_BUILD_PROGRAM_OWNERS" not in shared or
            "ASTRA_PROGRAM_OWNER_GATE" not in shared or
            "$(MAKE_RESTARTS)" not in shared or
            "ASTRA_PROGRAM_OWNERS_READY=1 program" not in shared):
        raise AssertionError(
            "sw/userspace/program.mk: direct program targets bypass library owners"
        )
    commands = (USERSPACE / "commands" / "Makefile").read_text()
    if ("ASTRA_PROGRAM_DIRECT_TARGETS := $(ASTRA_PROGRAM_TARGETS) $(TEST_IMAGES)"
            not in commands or
            "FULL_PROGRAM_GOALS := $(filter $(COMMAND_MANIFEST) size,$(MAKECMDGOALS))"
            not in commands or
            "$(COMMANDS) $(REQUESTED_COMMANDS)" not in commands):
        raise AssertionError(
            "sw/userspace/commands/Makefile: test or mixed command builds can "
            "bypass required library owners"
        )
    if re.search(r"^build/m68k/%\s*:", commands, re.MULTILINE):
        raise AssertionError(
            "sw/userspace/commands/Makefile: implicit command rule is hidden "
            "by direct-target owner prerequisites"
        )
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
        if ("dynamic-program.mk" not in text and
                "ASTRA_PROGRAM_OWNER_TARGETS" not in text):
            raise AssertionError(
                f"{relative(path)}: does not declare exact owner targets"
            )
        if re.search(r"^all\s*:", text, re.MULTILINE):
            raise AssertionError(
                f"{relative(path)}: defines program build order locally"
            )


def test_direct_parallel_build_cannot_observe_stale_owner_products():
    version = subprocess.run(
        ("make", "--version"), text=True, capture_output=True, check=True
    ).stdout
    match = re.search(r"GNU Make (\d+)\.(\d+)", version)
    if match is None or tuple(map(int, match.groups())) < (4, 0):
        return
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        owner = root / "owner"
        owner.mkdir()
        (root / "library").write_text("old\n")
        (owner / "source").write_text("new\n")
        (owner / "Makefile").write_text(
            "refresh:\n"
            "\t@sleep 0.25\n"
            "\tcp source ../library\n"
        )
        (root / "Makefile").write_text(
            "ASTRA_PROGRAM_OWNER_TARGETS := owner:refresh\n"
            "ASTRA_PROGRAM_TARGETS := image\n"
            "TARGET := output\n"
            "include " + str(USERSPACE / "program.mk") + "\n"
            "image: output\n"
            "output: library\n"
            "\t@test \"$$(cat library)\" = new\n"
            "\tcp library output\n"
        )
        result = subprocess.run(
            ("make", "-j2", "image"),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=10,
            check=False,
        )
        output = root / "output"
        if (result.returncode != 0 or not output.exists() or
                output.read_text() != "new\n"):
            raise AssertionError(
                "sw/userspace/program.mk: parallel direct build observed a "
                "stale owner product\n" + result.stdout + result.stderr
            )


def test_proc_text_views_are_streamed_without_aggregate_ceiling():
    source = (USERSPACE / "supervisor" / "src" / "proc_tree.c").read_text()
    if re.search(r"static\s+char\s+render\s*\[", source):
        raise AssertionError(
            "sw/userspace/supervisor/src/proc_tree.c: PROC text is capped by "
            "an aggregate render buffer"
        )


def test_linker_uses_the_canonical_address_map():
    header = (REPOSITORY / "sw" / "include" / "astra" /
              "address_space.h").read_text()
    linker = (USERSPACE / "runtime" / "astra_user_contract.ld").read_text()

    def hex_constant(text, name):
        match = re.search(
            rf"^\s*(?:#define\s+)?{name}\s*(?:=\s*)?(0x[0-9a-fA-F]+)",
            text,
            re.MULTILINE,
        )
        if match is None:
            raise AssertionError(f"missing address constant {name}")
        return int(match.group(1), 16)

    if hex_constant(linker, "ASTRA_TEXT_BASE") != hex_constant(
            header, "ASTRA_EXECUTABLE_LINK_ADDRESS"):
        raise AssertionError(
            "runtime linker text base disagrees with astra/address_space.h"
        )
    if hex_constant(linker, "ASTRA_EXECUTABLE_ADDRESS_END") != hex_constant(
            header, "ASTRA_EXECUTABLE_ADDRESS_END"):
        raise AssertionError(
            "runtime linker image ceiling disagrees with "
            "astra/address_space.h"
        )


def main():
    tests = (
        test_kernel_private_headers_do_not_cross_into_userspace,
        test_terminal_does_not_depend_on_supervisor_implementation,
        test_filesystem_private_state_stays_in_vfs,
        test_service_startup_protocol_is_shared,
        test_program_startup_capability_lookup_is_shared,
        test_native_commands_use_runtime_argument_access,
        test_posix_command_uses_the_vfs_path_authority,
        test_commands_do_not_restore_private_formatting_caps,
        test_commands_use_shared_stream_writes,
        test_endian_primitives_are_shared,
        test_checked_integer_primitives_are_shared,
        test_reserved_word_validation_is_shared,
        test_standard_string_primitives_are_shared,
        test_kernel_byte_comparison_is_shared,
        test_analyzer_targets_do_real_work,
        test_owner_analyzers_cover_production_sources,
        test_loadable_libraries_use_owner_built_archives,
        test_shared_library_products_have_one_registry,
        test_library_contracts_match_exact_readelf_fields,
        test_link_products_depend_on_their_build_contracts,
        test_services_do_not_compile_private_ndk_or_config_copies,
        test_static_archives_are_exact_replacements,
        test_posix_dependencies_have_one_recursive_build_barrier,
        test_cxx_command_uses_the_shared_runtime,
        test_ntpd_uses_a_required_dynamic_config_dependency,
        test_native_gui_programs_use_the_eager_dynamic_loader,
        test_ordinary_native_programs_share_the_dynamic_link_contract,
        test_shared_libraries_have_one_loader_and_one_symbol_abi,
        test_shared_libraries_have_one_provider_resolver,
        test_terminal_programs_share_terminfo,
        test_graphics_library_objects_track_all_inputs,
        test_interface_library_objects_track_all_inputs,
        test_supervisor_owns_local_service_endpoint_lifetimes,
        test_userspace_orchestration_does_not_overlap_shared_producers,
        test_ndk_private_headers_stay_in_ndk,
        test_public_headers_are_imported_by_namespace,
        test_ndk_owns_user_facing_headers,
        test_gui_wire_types_are_owned_by_the_protocol,
        test_message_header_serialization_is_shared,
        test_ascii_case_primitives_are_shared,
        test_graphics_geometry_is_shared_within_graphics,
        test_immutable_vfs_operations_are_shared,
        test_compiler_barrier_is_shared,
        test_runtime_diagnostics_and_time_are_shared,
        test_identical_device_callbacks_are_not_duplicated,
        test_program_build_order_is_shared,
        test_direct_parallel_build_cannot_observe_stale_owner_products,
        test_proc_text_views_are_streamed_without_aggregate_ceiling,
        test_linker_uses_the_canonical_address_map,
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
