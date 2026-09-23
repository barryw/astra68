#!/usr/bin/env python3
"""Positive and negative contracts for astra-prof profile handling."""

import importlib.machinery
import importlib.util
import contextlib
import io
import tempfile
from pathlib import Path


tool = Path(__file__).parents[1] / "astra-prof"
loader = importlib.machinery.SourceFileLoader("astra_prof", str(tool))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)

with tempfile.TemporaryDirectory() as directory:
    good = Path(directory) / "good.aprof"
    good.write_text(
        "interval\twarm-1\t100\t2100\n"
        "block\t0x02001000\t0x00101000\t4\t8\t10\t3\t2\t12\t8\t4e714e75\n"
        "caller\t0x02001000\t0x02002004\t7\n"
        "value\t0x02001000\td0\t0x00000008\t11\n"
        "end\n"
        "interval\twarm-2\t3000\t6000\n"
        "block\t0x02001000\t0x00101000\t4\t8\t20\t6\t4\t24\t16\t4e714e75\n"
        "end\n")
    intervals = module.parse_profile(good)
    assert len(intervals) == 2
    assert intervals[0]["callers"] == [
        {"probe": 0x02001000, "return": 0x02002004, "count": 7}]
    assert intervals[0]["values"] == [
        {"probe": 0x02001000, "register": "d0", "value": 8,
         "count": 11}]
    assert module.interval_totals(intervals[0]) == {
        "elapsed_ns": 2000, "instructions": 40, "blocks": 10,
        "loads": 3, "stores": 2, "load_bytes": 12, "store_bytes": 8,
    }
    assert module.median_totals(intervals)["instructions"] == 60
    assert module.median_totals(intervals)["loads"] == 4.5

    bad = Path(directory) / "bad.aprof"
    bad.write_text("interval\tbroken\t0\t1\nblock\ttoo-short\n")
    try:
        module.parse_profile(bad)
        raise AssertionError("truncated input was accepted")
    except ValueError:
        pass

    zero = Path(directory) / "zero.aprof"
    zero.write_text(
        "interval\tzero\t0\t1\n"
        "block\t0x02001000\t0x00101000\t1\t2\t1\t0\t0\t0\t0\t4e71\n"
        "end\n")
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        module.command_compare(type("Args", (), {
            "baseline": str(zero), "candidate": str(zero)})())
    assert "+inf" not in output.getvalue()
    assert "0.00%" in output.getvalue()
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        module.command_compare(type("Args", (), {
            "baseline": str(zero), "candidate": str(good)})())
    assert "n/a" in output.getvalue()

    control = Path(directory) / "control.jsonl"
    control.write_text(
        '{"run":1,"milliseconds":820.0,"guest-cycles":10250000}\n'
        '{"run":2,"milliseconds":824.0,"guest-cycles":10300000}\n'
        '{"runs":2,"milliseconds_median":822.0}\n')
    candidate = Path(directory) / "candidate.jsonl"
    candidate.write_text(
        '{"run":1,"milliseconds":810.0,"guest-cycles":10100000}\n'
        '{"run":2,"milliseconds":814.0,"guest-cycles":10150000}\n'
        '{"runs":2,"milliseconds_median":812.0}\n')
    assert module.load_latency_rows([control]) == [
        (820.0, 10250000), (824.0, 10300000)]
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        module.command_compare_latency(type("Args", (), {
            "baseline": [str(control)],
            "candidate": [str(candidate), str(candidate)]})())
    assert "812.000" in output.getvalue()
    assert "4" in output.getvalue()

    invalid_latency = Path(directory) / "invalid-latency.jsonl"
    invalid_latency.write_text(
        '{"run":1,"milliseconds":0,"guest-cycles":1}\n')
    try:
        module.load_latency_rows([invalid_latency])
        raise AssertionError("invalid latency row was accepted")
    except ValueError:
        pass

    incomplete_latency = Path(directory) / "incomplete-latency.jsonl"
    incomplete_latency.write_text(
        '{"run":1,"milliseconds":810,"guest-cycles":10100000}\n')
    try:
        module.load_latency_rows([incomplete_latency])
        raise AssertionError("incomplete latency artifact was accepted")
    except ValueError:
        pass

assert module.parse_image("kernel@0x02000000=kernel.elf") == {
    "name": "kernel", "base": 0x02000000, "path": "kernel.elf"}
try:
    module.parse_image("broken")
    raise AssertionError("invalid image was accepted")
except Exception:
    pass

image = {"name": "zsh", "bias": 0, "addresses": [0x1000],
         "symbols": [(0x1000, 4, "entry")],
         "segments": [(0x1000, bytes.fromhex("4e714e75"))]}
assert module.resolve_image([image], 0x1000, "4e714e75")[:2] == \
    ("zsh", "entry")
assert module.resolve_image([image], 0x1000, "4e754e71")[:2] == \
    ("unknown", "0x00001000")
assembly = {**image, "name": "kernel", "addresses": [0x1000],
            "symbols": [(0x1000, 0x30, "copy_from_user")],
            "segments": [(0x1000, bytes.fromhex("4e71") * 0x18)]}
assert module.resolve_image([assembly], 0x1020, "4e714e71")[:2] == \
    ("kernel", "copy_from_user")
zero_size = {**assembly, "symbols": [(0x1000, 0, "copy_from_user")]}
assert module.resolve_image([zero_size], 0x1020, "4e714e71")[:2] == \
    ("unknown", "0x00001020")
duplicate = {**image, "name": "terminal"}
assert module.resolve_image([image, duplicate], 0x1000, "4e714e75")[:2] == \
    ("shared(2)", "entry")
conflict = {**image, "name": "other",
            "symbols": [(0x1000, 4, "different")]}
assert module.resolve_image([image, conflict], 0x1000, "4e714e75")[:2] == \
    ("unknown", "0x00001000")
assert module.link_address({"bias": 0x20000000}, 0x20001000) == 0x1000
assert module.link_address({"bias": 0}, 0x1000) == 0x1000
annotation_intervals = [
    {"blocks": [{"vaddr": 0x1000, "bytes": 4, "insns": 2,
                  "executions": 3, "signature": "4e714e75"}]},
    {"blocks": [{"vaddr": 0x1000, "bytes": 4, "insns": 2,
                  "executions": 5, "signature": "4e714e75"},
                 {"vaddr": 0x2000, "bytes": 4, "insns": 2,
                  "executions": 7, "signature": "4e714e75"}]},
]
annotated = module.annotated_blocks(annotation_intervals, image)
assert len(annotated) == 1 and annotated[0]["executions"] == 8

signature = "00112233445566778899aabbccddeeff"
relocatable = {
    "name": "system.library.2", "path": "loader.library.1",
    "base": None, "bias": 0,
    "relocatable": True, "addresses": [0x1000],
    "symbols": [(0x1000, 0x20, "astra_call")],
    "segments": [(0x1000, bytes.fromhex(signature) + b"\0" * 16)],
    "executable_segments": [
        (0x1000, bytes.fromhex(signature) + b"\0" * 16)],
}
relocation_profile = [{"blocks": [{
    "vaddr": 0x20001000, "paddr": 0x12345000, "signature": signature}]}]
inferred = module.relocate_images(relocation_profile, [relocatable])
assert len(inferred) == 1 and inferred[0]["bias"] == 0x20000000
assert module.resolve_image(inferred, 0x20001000, signature)[:2] == \
    ("system.library.2", "astra_call")
ambiguous = {**relocatable, "executable_segments": [
    (0x1000, bytes.fromhex(signature) * 2)]}
assert module.relocate_images(relocation_profile, [ambiguous]) == []
conflicting_profile = [{"blocks": [
    {"vaddr": 0x20001000, "paddr": 1, "signature": signature},
    {"vaddr": 0x30001000, "paddr": 2, "signature": signature},
]}]
assert module.relocate_images(conflicting_profile, [relocatable]) == []

with tempfile.TemporaryDirectory() as directory:
    profile = Path(directory) / "relocated.aprof"
    profile.write_text(
        "interval\tcommand-1\t0\t1\n"
        f"block\t0x20001000\t0x12345000\t16\t8\t1\t0\t0\t0\t0\t{signature}\n"
        "end\n")
    original_load_symbols = module.load_symbols
    original_run = module.subprocess.run
    commands = []
    module.load_symbols = lambda _image, _nm: relocatable
    module.subprocess.run = lambda command, check: commands.append(command)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            module.command_annotate(type("Args", (), {
                "profile": str(profile), "image": {}, "nm": "nm",
                "objdump": "objdump", "limit": 1})())
    finally:
        module.load_symbols = original_load_symbols
        module.subprocess.run = original_run
    assert "--start-address=4096" in commands[0]

    module.load_symbols = lambda _image, _nm: ambiguous
    try:
        try:
            module.command_annotate(type("Args", (), {
                "profile": str(profile), "image": {}, "nm": "nm",
                "objdump": "objdump", "limit": 1})())
            raise AssertionError("ambiguous annotation image was accepted")
        except RuntimeError:
            pass
    finally:
        module.load_symbols = original_load_symbols

catalog = {"0x1000": {"format": "shell ready"}}
assert module.find_event_message(catalog, "shell ready") == 0x1000
for invalid in ({}, {"0x1000": {"format": "shell ready"},
                     "0x2000": {"format": "shell ready"}}):
    try:
        module.find_event_message(invalid, "shell ready")
        raise AssertionError("ambiguous or absent event was accepted")
    except RuntimeError:
        pass

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    image_path = root / "sw/userspace/services/demo/build/m68k/demo.elf"
    library_path = root / \
        "sw/userspace/runtime/build/m68k/libraries/runtime.library.1"
    ndk_path = root / "ndk/build/m68k/libraries/system.library.2"
    image_path.parent.mkdir(parents=True)
    library_path.parent.mkdir(parents=True)
    ndk_path.parent.mkdir(parents=True)
    image_path.write_bytes(b"\x7fELF\x01\x02")
    library_path.write_bytes(b"\x7fELF\x01\x02")
    ndk_path.write_bytes(b"\x7fELF\x01\x02")
    (image_path.parent / "ignored.bin").touch()
    (image_path.parent / "ignored.image.elf").write_bytes(b"\x7fELF\x01\x02")
    (library_path.parent / "ignored.o").touch()
    (library_path.parent / "ignored.library.exports").write_text("not ELF")
    discovered = module.target_images(root)
    assert {image["path"] for image in discovered} == {
        str(image_path), str(library_path), str(ndk_path)}

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    script = root / "emu/qemu/astra_image.py"
    makefile = root / "sw/userspace/storage/Makefile"
    script.parent.mkdir(parents=True)
    makefile.parent.mkdir(parents=True)
    script.write_text("image-v1")
    makefile.write_text("format-v1")
    first = module.storage_fingerprint(root)
    assert module.storage_fingerprint(root) == first
    makefile.write_text("format-v2")
    assert module.storage_fingerprint(root) != first

    nested = root / "sw/userspace/kits/build/Runtime.kit/libraries/runtime"
    nested.parent.mkdir(parents=True)
    nested.write_text("runtime-v1")
    with_nested = module.storage_fingerprint(root)
    nested.write_text("runtime-v2")
    assert module.storage_fingerprint(root) != with_nested

    outside = root / "sw/userspace/kits/not-installed"
    outside.write_text("ignored-v1")
    ignored = module.storage_fingerprint(root)
    outside.write_text("ignored-v2")
    assert module.storage_fingerprint(root) == ignored

parsed = module.parser().parse_args(
    ["record", "--output", "profile.aprof", "--", "qemu-system-m68k"])
assert parsed.command == ["--", "qemu-system-m68k"]
parsed = module.parser().parse_args(
    ["de25-command", "--output", "/tmp/profile", "--cleanup", "reset",
     "--probe", "0x02001000", "--probe-register", "d0",
     "which", "status"])
assert parsed.profile_mode == "command"
assert parsed.profile_command == ["which", "status"]
assert parsed.cleanup == "reset"
assert parsed.probe == [0x02001000]
assert parsed.probe_register == "d0"
assert parsed.runs == 5 and parsed.warmups == 2
assert parsed.stop_marker == "stage 8"
latency = module.parser().parse_args(
    ["de25-latency", "--output", "/tmp/latency", "status"])
assert latency.profile_mode == "latency"
assert latency.max_milliseconds == 500.0
assert latency.profile_command == ["status"]
paired = module.parser().parse_args([
    "compare-latency", "--baseline", "/tmp/control",
    "--candidate", "/tmp/before", "--candidate", "/tmp/after"])
assert paired.baseline == ["/tmp/control"]
assert paired.candidate == ["/tmp/before", "/tmp/after"]
assert module.parser().parse_args(
    ["de25-boot", "--output", "/tmp/profile"]).stop_marker == "stage 8"
for invalid in ("-1", "0x100000000", "broken"):
    try:
        module.parse_address(invalid)
        raise AssertionError("invalid probe address was accepted")
    except Exception:
        pass
for invalid in ("", "d0,register=d1", "d0\nvalue"):
    try:
        module.parse_register(invalid)
        raise AssertionError("invalid register name was accepted")
    except Exception:
        pass

tool_source = tool.read_text()
assert 'command_text == "zsh"' not in tool_source
assert "zsh-astra-build" not in tool_source

empty = type("Args", (), {"rom": None, "storage": None, "kernel": None,
                           "rom_elf": None})()
assert not module.custom_software_artifacts(empty)
complete = type("Args", (), {"rom": "r", "storage": "s", "kernel": "k",
                              "rom_elf": "e"})()
assert module.custom_software_artifacts(complete)
partial = type("Args", (), {"rom": "r", "storage": None, "kernel": None,
                             "rom_elf": None})()
try:
    module.custom_software_artifacts(partial)
    raise AssertionError("mismatched software artifacts were accepted")
except RuntimeError:
    pass

board_script = (tool.parent / "astra-prof-de25-boot.sh").read_text()
for required in ("trap restore EXIT HUP INT TERM", "System degraded:",
                 "kill -0", "profile.aprof", "systemctl start astra.service",
                 "--dump-trace-ring", "host-perf.data", "report --stdio",
                 "--profile-control", "command-metrics.jsonl",
                 "--desktop-ready-message", "invalid probe address",
                 "invalid probe register"):
    assert required in board_script
shutdown = board_script.split('--dump-trace-ring "$work/trace.bin" || true', 1)[1]
assert shutdown.index('--quit') < shutdown.index('kill "$runner_pid"')
assert "perf report contained no samples" in shutdown
assert "'^# Samples: [1-9]'" in shutdown
assert '[ "$mode" = latency ]' in board_script
assert '--max-milliseconds "$max_milliseconds"' in board_script
assert board_script.count('--deadline "$timeout"') == 4
assert 'case "$timeout" in \'\'|*[!0-9]*|0)' in board_script
assert 'ASTRA_BASE_STORAGE="$storage"' in board_script

perf_wrapper = (tool.parent / "qemu-perf-wrapper.sh").read_text()
assert 'PERF=${ASTRA_PERF_BINARY:-perf}' in perf_wrapper
assert 'exec "$PERF" record' in perf_wrapper

plugin_source = (tool.parents[1] / "emu/qemu/qemu-9.2/contrib/plugins/astra_profile.c").read_text()
assert "qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu" in plugin_source
assert "baseline_executions" in plugin_source
assert "qemu_plugin_register_vcpu_tb_exec_cb" in plugin_source
assert "if (key.vaddr == probe)" in plugin_source
assert "QEMU_PLUGIN_CB_R_REGS" in plugin_source
assert 'g_strcmp0(option[0], "probe")' in plugin_source

print("astra-prof tests: PASS")
