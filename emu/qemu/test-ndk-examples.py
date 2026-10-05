#!/usr/bin/env python3
"""Run every NDK example, as built from the extracted NDK archive.

`make -C ndk dist-check` builds `ndk/examples` with the archive's own make
fragments, headers and libraries into EXAMPLES (one ELF per example). This
gate installs each as `/local/commands/ndk-<name>`, boots the terminal
image, runs it from zsh and requires its line of output and exit status 0.
An example without an expected line here, or an expected line without an
example, fails the gate: a new example cannot ship unrun.
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.environ.get("ASTRA_QEMU_TEST_ROOT",
                      os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)
astra_image = terminal.astra_image
SPEC = importlib.util.spec_from_file_location(
    "astra_sdl_audio_gate", os.path.join(HERE, "test-sdl-audio.py"))
audio_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audio_gate)


def heard(host, name):
    """Why the stand-in host did not hear example @p name, or None."""
    with host.lock:
        host.drain()
        if name == "pcm_tone":
            voices = [v for v in host.voices
                      if v.format == PCM_TONE_FORMAT and v.generation]
            if not voices:
                return "no audio stream in its format"
            if not any(voices[0].written):
                return "its audio stream was silent"
        if name == "midi_notes":
            synths = list(host.midis.values()) + host.closed_midis
            if not any(m.energy > 0.0 for m in synths):
                return "the synthesizer made no sound"
    return None

# What each example prints when it worked. Values that depend on the
# machine's speed or the clock are not part of any line.
EXPECTED = {
    "hello": "Hello from Astra",
    "timer": "timer: 5 ticks on deadline, cancelled timer stayed quiet",
    "port_message": "port_message: ping 42 received as transaction 7",
    "bulk_ring": "bulk_ring: 64 records in order, 3 to 66",
    "undo": "undo: zoom 150 -> undo 100 -> redo 150 in 1 coalesced group",
    # The field's text ends in two CJK scalars the trace reader does not
    # decode, so the line is matched by its byte count (16 + 6 + 1).
    "text_field": "(23 bytes)",
    "text_clipboard": 'text_clipboard: copied and pasted "Astra"',
    "interface_controls":
        "interface_controls: Tab, Enter turned Snap to grid on",
    "interface_tabs":
        "interface_tabs: Tab, Right showed Advanced and collapsed General",
    "interface_scroll":
        "interface_scroll: one wheel notch scrolled to 16 of 400",
    "interface_splitter": "interface_splitter: Tab, Right moved the divider",
    "interface_disclosure":
        "interface_disclosure: Tab, Enter collapsed Advanced",
    "pcm_tone": "pcm_tone: 50 buffers of 480 frames, 440 Hz, played in place",
    "midi_notes": "midi_notes: C E G C on the host's wavetable",
}
# The audio examples must also have been heard by the stand-in host
# (test-sdl-audio.py's AudioHost): pcm_tone through its own audio stream,
# midi_notes on the host's synthesizer.
PCM_TONE_FORMAT = 2 | 1 << 8 | 48000 << 12  # S16BE mono 48 kHz
EXIT_MARK = "NDK-EXAMPLE-EXIT-"


def run_example(machine, name, deadline):
    """Return (lines, exit status), or (lines, None) if zsh never answered."""
    before = machine.sequence()
    # The quotes keep the echoed command line from matching the mark.
    machine.qmp.type_line('ndk-%s; print NDK-EXAMPLE-""EXIT-$?' % name)
    said = terminal.wait_for_command(machine, EXIT_MARK, deadline, before)
    if said is None:
        return machine.said(before)[0], None
    for line in said:
        match = re.search(re.escape(EXIT_MARK) + r"(\d+)", line)
        if match:
            return said, int(match.group(1))
    return said, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("examples",
                        help="directory of example ELFs built from the "
                             "NDK archive (ndk/build/dist/examples)")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    args = parser.parse_args()

    built = sorted(entry[:-len(".elf")] for entry in os.listdir(args.examples)
                   if entry.endswith(".elf"))
    missing = sorted(set(EXPECTED) - set(built))
    unexpected = sorted(set(built) - set(EXPECTED))
    if missing or unexpected:
        print("FAIL: examples built %s; no ELF for %s; no expectation for %s"
              % (built, missing, unexpected))
        return 1
    if not terminal.refresh_workspace_rom(args.rom):
        return 1
    failures = []
    with tempfile.TemporaryDirectory(prefix="astra-ndk-examples-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(args.image, scratch)
        astra_image.install(scratch, args.catalog, vim_runtime=None)
        for name in built:
            astra_image._replace_volume_file(
                scratch, os.path.join(args.examples, name + ".elf"),
                "/%s/ndk-%s" % (astra_image.LOCAL_COMMANDS_DIRECTORY, name))
        run_dir = os.path.join(temporary, "run")
        os.mkdir(run_dir)
        host = audio_gate.AudioHost(os.path.join(run_dir, "audio.sock"))
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = os.path.join(run_dir,
                                                             "audio.sock")
        machine = terminal.Machine(args.qemu, args.rom, scratch, run_dir)
        try:
            if not terminal.open_terminal(machine, args.boot_deadline,
                                          args.command_deadline):
                return 1
            for name in built:
                said, status = run_example(machine, name,
                                           args.command_deadline)
                shown = [line for line in said if EXPECTED[name] in line]
                silent = heard(host, name) if status == 0 and shown else None
                if silent is not None:
                    failures.append(name)
                    print("FAIL %s: %s" % (name, silent))
                    continue
                if status == 0 and shown:
                    print("PASS %s: %s" % (name, shown[0].strip()))
                    continue
                failures.append(name)
                print("FAIL %s (exit %s)" % (name, status))
                for line in said[-20:]:
                    print("    |%s|" % line)
        finally:
            machine.close()
            host.close()
    if failures:
        print("FAIL: test-ndk-examples: %s" % " ".join(failures))
        return 1
    print("ASTRA NDK EXAMPLES PASS %d examples" % len(built))
    return 0


if __name__ == "__main__":
    sys.exit(main())
