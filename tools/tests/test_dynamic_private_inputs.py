import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
SHARED_POLICY = ROOT / "mk/astra-shared-library.mk"


def makefile(path):
    return (ROOT / path).read_text()


def assignment(text, name):
    lines = text.splitlines()
    prefix = f"{name} :="
    for index, line in enumerate(lines):
        if not line.startswith(prefix):
            continue
        value = line.removeprefix(prefix).strip()
        while value.endswith("\\"):
            index += 1
            value = value[:-1] + " " + lines[index].strip()
        return value
    raise AssertionError(f"missing {name}")


def test_dynamic_private_archive_sources_are_pic():
    for path in (
        "sw/userspace/terminal/Makefile",
        "sw/userspace/input/Makefile",
        "sw/userspace/events/Makefile",
        "sw/userspace/metrics/Makefile",
    ):
        text = makefile(path)
        flags = assignment(text, "TARGET_FLAGS")
        assert "-fPIC" in flags
        assert "-ftls-model=initial-exec" in flags
        assert "-ftls-model=global-dynamic" not in flags
        assert re.search(r"^\$\((?:TARGET_OBJECTS|OBJECTS)\): Makefile$", text,
                         re.MULTILINE)


def test_dynamic_program_policy_requires_loader_supported_tls():
    policy = makefile("sw/userspace/dynamic-program.mk")
    assert "TARGET_FLAGS += -ftls-model=initial-exec" in policy
    assert "-ftls-model=global-dynamic" not in policy


def test_shared_objects_rebuild_and_use_loader_supported_tls():
    policy = SHARED_POLICY.read_text()
    assert "override .EXTRA_PREREQS += $(ASTRA_SHARED_OWNER_MAKEFILE)" in policy
    assert "$(ASTRA_EXISTING_PRODUCTS): .EXTRA_PREREQS +=" in policy
    assert "ASTRA_SHARED_PIC_FLAGS := -fPIC -ftls-model=initial-exec" in policy
    assert "-ftls-model=global-dynamic" not in policy

    for path in ("sw/userspace/config/Makefile",
                 "sw/userspace/loader/Makefile"):
        flags = assignment(makefile(path), "TARGET_FLAGS")
        assert "$(ASTRA_SHARED_PIC_FLAGS)" in flags
        assert "-fPIC" not in flags


def test_shared_library_gate_accepts_required_and_rejects_forbidden_layouts():
    policy = makefile("mk/m68k-cross.mk")
    gate = policy.split("define ASTRA_CHECK_DSO", 1)[1].split(
        "endef", 1)[0]
    for required in ("Type:.*DYN", " DYNAMIC ", " GNU_RELRO ",
                     "SONAME", "BIND_NOW"):
        assert required in gate
    assert "! printf '%s\\n' \"$$program\" | grep -q ' INTERP '" in gate
    assert "! printf '%s\\n' \"$$dynamic\" | grep -q TEXTREL" in gate


def test_dynamic_private_archives_reject_non_pic_object_sets():
    network = makefile("sw/userspace/network/Makefile")
    service_rule = re.search(r"^\$\(SERVICE_TARGET\): (.*)$", network,
                             re.MULTILINE)
    assert service_rule
    assert "build/m68k/library/network_core.o" in service_rule.group(1)
    assert "build/m68k/network_core.o" not in service_rule.group(1)

    cases = (
        ("sw/userspace/graphics/Makefile", "RENDERER_OBJECTS",
         "build/m68k/pic/render_builder.o", "build/m68k/render_builder.o"),
        ("sw/userspace/vfs/Makefile", "SERVICE_OBJECTS",
         "build/m68k/library/", "build/m68k/vfs_service_core.o"),
    )
    for path, name, required, forbidden in cases:
        value = assignment(makefile(path), name)
        assert required in value
        assert forbidden not in value


def test_cxx_contract_rebuilds_when_a_dependency_changes():
    text = makefile("sw/userspace/cxx/Makefile").replace("\\\n", " ")
    rule = re.search(r"^\$\(LIBRARY_CONTRACT_STAMP\): (.*)$", text,
                     re.MULTILINE)
    assert rule
    normal = rule.group(1).partition("|")[0]
    for dependency in ("$(LIBC_LIBRARY)", "$(RUNTIME_LIBRARY)",
                       "$(COMPILER_LIBRARY)", "$(UNWIND_LIBRARY)"):
        assert dependency in normal


def test_cxx_contract_does_not_rebuild_for_phony_readiness_alone():
    text = makefile("sw/userspace/cxx/Makefile").replace("\\\n", " ")
    rule = re.search(r"^\$\(LIBRARY_CONTRACT_STAMP\): (.*)$", text,
                     re.MULTILINE)
    assert rule
    normal, separator, order_only = rule.group(1).partition("|")
    assert separator
    assert "unwind-library-ready" not in normal
    assert "unwind-library-ready" in order_only
