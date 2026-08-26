#!/usr/bin/env python3
"""Boot a clear desktop, launch Terminal by double-click, and verify GUI I/O."""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

BOOT_MARKER = "stage 8"
DISPLAY_QUEUE = 0xFFF001E0
ASTRAEA_IRQ_STATUS = 0xFFF10014
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0xFF
QUEUE_REQUEST_READY = 1 << 8
DRAW_DONE = 1 << 3
PRESENT_BUDGET_CYCLES = 250000
POINTER_BUDGET_CYCLES = 250000
RESIZE_BUDGET_CYCLES = 250000
NOMINAL_CPU_HZ = 12500000
EXPECTED_SUBMISSIONS = 2
EXPECTED_BATCHES = 1
EXPECTED_POINTER_SUBMISSIONS = 1
EXPECTED_POINTER_BATCHES = 0
RENDER_OPERATION = 3
CURSOR_OPERATION = 4


class Qmp:
    def __init__(self, path, deadline=20.0):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        end = time.monotonic() + deadline
        while True:
            try:
                self.socket.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= end:
                    raise
                time.sleep(0.05)
        self.file = self.socket.makefile("rwb", buffering=0)
        self.file.readline()
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request).encode("ascii") + b"\n")
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError("QMP %s failed: %s" %
                                   (command, reply["error"]))
            return reply.get("return")

    def monitor(self, line):
        return self.execute("human-monitor-command", {"command-line": line})

    def property(self, name):
        return self.execute("qom-get", {"path": "/machine",
                                        "property": name})

    def word(self, address):
        for token in self.monitor("xp /1xw 0x%x" % address).split():
            if token.startswith("0x") and len(token) == 10:
                return int(token, 16)
        raise RuntimeError("no word read back from 0x%x" % address)

    def key(self, qcode):
        for down in (True, False):
            self.execute("input-send-event", {"events": [{
                "type": "key", "data": {"down": down,
                "key": {"type": "qcode", "data": qcode}}}]})
            time.sleep(0.02)

    def move(self, x, y):
        self.execute("input-send-event", {"events": [{
            "type": "abs", "data": {"axis": "x", "value": x}}, {
            "type": "abs", "data": {"axis": "y", "value": y}}]})
        time.sleep(0.02)

    def button(self, down):
        self.execute("input-send-event", {"events": [{
            "type": "btn", "data": {"button": "left", "down": down}}]})
        time.sleep(0.02)

    def click(self):
        self.button(True)
        self.button(False)


def run(qemu, rom, image, catalog, deadline):
    with tempfile.TemporaryDirectory(prefix="astra-display-") as directory:
        socket_path = os.path.join(directory, "display-qmp.sock")
        scratch = os.path.join(directory, "card.img")
        shutil.copyfile(image, scratch)
        astra_image.install(
            scratch, catalog,
            service_names=astra_image.DISPLAY_SERVICES,
            manifest_text=astra_image.DISPLAY_STARTUP_MANIFEST)
        machine = subprocess.Popen(
            [qemu, "-M", "astra68", "-m", "128M", "-bios", rom,
             "-display", "none", "-monitor", "none", "-serial", "stdio",
             "-no-reboot", "-qmp", "unix:%s,server=on,wait=off" % socket_path,
             "-drive", "if=none,format=raw,file=%s" % scratch],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        booted = threading.Event()
        serial = []

        def drain():
            for line in machine.stdout:
                line = line.rstrip("\n")
                serial.append(line)
                if BOOT_MARKER in line:
                    booted.set()

        threading.Thread(target=drain, daemon=True).start()
        started = time.monotonic()
        try:
            qmp = Qmp(socket_path)
            end = time.monotonic() + deadline
            last_counts = (0, 0, 0)
            while not booted.is_set() and machine.poll() is None and \
                    time.monotonic() < end:
                try:
                    last_counts = (
                        qmp.property("astra-display-submissions"),
                        qmp.property("astra-display-completions"),
                        qmp.property("astra-display-generation"))
                except (BrokenPipeError, ConnectionError):
                    break
                time.sleep(0.05)
            if not booted.is_set():
                registers = "unavailable"
                if machine.poll() is None:
                    try:
                        registers = qmp.monitor("info registers").strip()
                    except (BrokenPipeError, ConnectionError):
                        pass
                time.sleep(0.05)
                raise RuntimeError(
                    "no %r; exit=%s display requests=%d completions=%d "
                    "generation=%d; registers: %s; last serial: %s" %
                    (BOOT_MARKER, machine.poll(), *last_counts,
                     registers, serial[-40:]))
            benchmark = next((line for line in serial
                              if line.startswith("CPU benchmark ...... ")), "")
            try:
                measured_hz = int(benchmark.split()[3])
                benchmark_us = int(benchmark.split()[8])
            except (IndexError, ValueError):
                raise RuntimeError("missing CPU throughput measurement: %s" %
                                   serial[-40:])
            if measured_hz <= NOMINAL_CPU_HZ:
                raise RuntimeError("CPU throughput did not exceed timer rate: "
                                   "%d Hz" % measured_hz)
            if benchmark_us <= 0 or benchmark_us > 10000000:
                raise RuntimeError("invalid CPU benchmark time: %d us" %
                                   benchmark_us)
            settle_deadline = time.monotonic() + 2.0
            while (qmp.property("astra-display-submissions") <
                   EXPECTED_SUBMISSIONS or
                   qmp.property("astra-display-completions") <
                   EXPECTED_SUBMISSIONS) and \
                    time.monotonic() < settle_deadline:
                time.sleep(0.01)
            elapsed = time.monotonic() - started
            submissions = qmp.property("astra-display-submissions")
            completions = qmp.property("astra-display-completions")
            generation = qmp.property("astra-display-generation")
            submit_cycle = qmp.property("astra-display-submit-cycle")
            completion_cycle = qmp.property(
                "astra-display-completion-cycle")
            collect_cycle = qmp.property("astra-display-collect-cycle")
            operation = qmp.property("astra-display-operation")
            batches = qmp.property("astra-display-render-batches")
            commands = qmp.property("astra-display-render-commands")
            fills = qmp.property("astra-display-fill-commands")
            blits = qmp.property("astra-display-blit-commands")
            glyphs = qmp.property("astra-display-glyph-commands")
            queue = qmp.word(DISPLAY_QUEUE)
            irq = qmp.word(ASTRAEA_IRQ_STATUS)
            if (submissions, completions) != (EXPECTED_SUBMISSIONS,
                                               EXPECTED_SUBMISSIONS):
                raise RuntimeError(
                    "display requests=%d completions=%d batches=%d "
                    "commands=%d fills=%d blits=%d glyphs=%d" %
                    (submissions, completions, batches, commands, fills,
                     blits, glyphs))
            if generation == 0:
                raise RuntimeError("display completion had no generation")
            if operation != RENDER_OPERATION or batches != EXPECTED_BATCHES:
                raise RuntimeError("display used operation=%d batches=%d" %
                                   (operation, batches))
            if commands == 0 or fills == 0 or blits == 0 or glyphs == 0:
                raise RuntimeError(
                    "hardware command mix commands=%d fills=%d blits=%d glyphs=%d" %
                    (commands, fills, blits, glyphs))
            if queue != QUEUE_REQUEST_READY:
                raise RuntimeError("display completion was not consumed: "
                                   "queue=0x%08x" % queue)
            if irq & DRAW_DONE:
                raise RuntimeError("display completion IRQ was not cleared")
            cursor_updates = qmp.property("astra-display-cursor-updates")
            cursor_x = qmp.property("astra-display-cursor-x")
            cursor_y = qmp.property("astra-display-cursor-y")
            cursor_visible = qmp.property("astra-display-cursor-visible")
            if (cursor_updates, cursor_x, cursor_y, cursor_visible) != \
                    (1, 640, 360, 1):
                raise RuntimeError(
                    "initial cursor updates=%d position=%d,%d visible=%d" %
                    (cursor_updates, cursor_x, cursor_y, cursor_visible))
            desktop_commands = commands
            desktop_fills = fills
            desktop_blits = blits
            desktop_glyphs = glyphs
            try:
                qmp.move(70, 90)
                launch_started = time.monotonic()
                qmp.click()
                time.sleep(0.08)
                qmp.click()
            except ConnectionError as error:
                raise RuntimeError(
                    "Terminal launch terminated machine exit=%s serial=%s" %
                    (machine.poll(), serial[-40:])) from error
            launch_deadline = time.monotonic() + 5.0
            while (qmp.property("astra-display-render-batches") <
                   EXPECTED_BATCHES + 2 or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < launch_deadline:
                time.sleep(0.01)
            launched_submissions = qmp.property(
                "astra-display-submissions")
            launched_batches = qmp.property("astra-display-render-batches")
            launched_commands = qmp.property("astra-display-render-commands")
            launched_fills = qmp.property("astra-display-fill-commands")
            launched_blits = qmp.property("astra-display-blit-commands")
            launched_glyphs = qmp.property("astra-display-glyph-commands")
            launch_elapsed = time.monotonic() - launch_started
            if launched_batches < EXPECTED_BATCHES + 2 or \
                    launched_submissions != qmp.property(
                        "astra-display-completions") or \
                    launched_commands <= desktop_commands or \
                    launched_fills <= desktop_fills or \
                    launched_blits <= desktop_blits or \
                    launched_glyphs <= desktop_glyphs:
                raise RuntimeError(
                    "Terminal double-click launch failed: requests=%d/%d "
                    "batches=%d commands=%d fills=%d blits=%d glyphs=%d" %
                    (launched_submissions,
                     qmp.property("astra-display-completions"),
                     launched_batches, launched_commands, launched_fills,
                     launched_blits, launched_glyphs))
            cursor_updates_before = qmp.property(
                "astra-display-cursor-updates")
            cursor_x_before = qmp.property("astra-display-cursor-x")
            cursor_y_before = qmp.property("astra-display-cursor-y")
            qmp.execute("input-send-event", {"events": [{
                "type": "rel", "data": {"axis": "x", "value": 5}}, {
                "type": "rel", "data": {"axis": "y", "value": 3}}]})
            pointer_deadline = time.monotonic() + 2.0
            while (qmp.property("astra-display-cursor-updates") ==
                       cursor_updates_before or
                   qmp.property("astra-display-completions") ==
                       launched_submissions) and \
                    time.monotonic() < pointer_deadline:
                time.sleep(0.01)
            cursor_updates = qmp.property("astra-display-cursor-updates")
            cursor_x = qmp.property("astra-display-cursor-x")
            cursor_y = qmp.property("astra-display-cursor-y")
            pointer_submissions = qmp.property("astra-display-submissions")
            pointer_completions = qmp.property("astra-display-completions")
            pointer_operation = qmp.property("astra-display-operation")
            pointer_batches = qmp.property("astra-display-render-batches")
            if cursor_updates != cursor_updates_before + 1 or \
                    cursor_x != cursor_x_before + 6 or \
                    cursor_y != cursor_y_before + 3 or \
                    pointer_submissions != (launched_submissions +
                                            EXPECTED_POINTER_SUBMISSIONS) or \
                    pointer_completions != (launched_submissions +
                                            EXPECTED_POINTER_SUBMISSIONS) or \
                    pointer_batches != launched_batches + \
                        EXPECTED_POINTER_BATCHES or \
                    pointer_operation != CURSOR_OPERATION:
                raise RuntimeError(
                    "pointer route updates=%d x=%d requests=%d/%d "
                    "batches=%d op=%d" %
                    (cursor_updates, cursor_x, pointer_submissions,
                     pointer_completions, pointer_batches,
                     pointer_operation))
            pointer_submit_cycle = qmp.property(
                "astra-display-cursor-submit-cycle")
            pointer_completion_cycle = qmp.property(
                "astra-display-cursor-completion-cycle")
            pointer_collect_cycle = qmp.property(
                "astra-display-cursor-collect-cycle")
            pointer_cycles = pointer_collect_cycle - pointer_submit_cycle
            if not (pointer_submit_cycle < pointer_completion_cycle <=
                    pointer_collect_cycle):
                raise RuntimeError(
                    "pointer cycle order invalid: %d %d %d" %
                                   (pointer_submit_cycle,
                                    pointer_completion_cycle,
                                    pointer_collect_cycle))
            if pointer_cycles > POINTER_BUDGET_CYCLES:
                raise RuntimeError("pointer update took %d cycles; budget %d" %
                                   (pointer_cycles,
                                    POINTER_BUDGET_CYCLES))
            queue = qmp.word(DISPLAY_QUEUE)
            irq = qmp.word(ASTRAEA_IRQ_STATUS)
            if queue != QUEUE_REQUEST_READY or irq & DRAW_DONE:
                raise RuntimeError(
                    "pointer completion not consumed: queue=0x%08x irq=0x%08x" %
                    (queue, irq))
            # Pointer traffic must not starve application timers.  Terminal
            # does not consume pointer events, so its underline must keep
            # blinking while the hardware cursor is moving.
            blink_batches = pointer_batches
            motion_end = time.monotonic() + 1.1
            motion_x = 700
            while time.monotonic() < motion_end:
                qmp.execute("input-send-event", {"events": [{
                    "type": "abs", "data": {"axis": "x",
                    "value": motion_x}}, {
                    "type": "abs", "data": {"axis": "y",
                    "value": 400}}]})
                motion_x = 701 if motion_x == 700 else 700
                time.sleep(0.0005)
            pointer_batches = qmp.property("astra-display-render-batches")
            if pointer_batches < blink_batches + 1:
                raise RuntimeError(
                    "pointer motion starved terminal cursor blink: "
                    "batches=%d/%d" % (blink_batches, pointer_batches))
            pointer_submissions = qmp.property("astra-display-submissions")
            pointer_completions = qmp.property("astra-display-completions")
            typed_submissions = pointer_submissions
            typed_batches = pointer_batches
            typed_glyphs = qmp.property("astra-display-glyph-commands")
            for qcode in ("p", "w", "d", "ret"):
                qmp.key(qcode)
            typed_deadline = time.monotonic() + 3.0
            while (qmp.property("astra-display-submissions") <
                   typed_submissions + 4 or
                   (qmp.word(INPUT_STATUS) & INPUT_COUNT_MASK) != 0) and \
                    time.monotonic() < typed_deadline:
                time.sleep(0.01)
            keyboard_submissions = qmp.property("astra-display-submissions")
            keyboard_completions = qmp.property("astra-display-completions")
            keyboard_batches = qmp.property("astra-display-render-batches")
            keyboard_glyphs = qmp.property("astra-display-glyph-commands")
            if keyboard_submissions < typed_submissions + 4 or \
                    keyboard_completions != keyboard_submissions or \
                    keyboard_batches <= typed_batches or \
                    keyboard_glyphs <= typed_glyphs or \
                    qmp.property("astra-display-operation") != \
                        RENDER_OPERATION or \
                    (qmp.word(INPUT_STATUS) & INPUT_COUNT_MASK) != 0:
                raise RuntimeError(
                    "keyboard route requests=%d/%d batches=%d glyphs=%d "
                    "input=0x%08x op=%d" %
                    (keyboard_submissions, keyboard_completions,
                     keyboard_batches, keyboard_glyphs,
                     qmp.word(INPUT_STATUS),
                     qmp.property("astra-display-operation")))
            resize_batches = keyboard_batches
            resize_glyphs = keyboard_glyphs
            resize_started = time.monotonic()
            try:
                qmp.move(1020, 578)
                qmp.button(True)
                qmp.move(1120, 658)
                qmp.button(False)
                resize_deadline = time.monotonic() + 3.0
                while (qmp.property("astra-display-render-batches") <=
                       resize_batches or
                       qmp.property("astra-display-glyph-commands") <=
                       resize_glyphs or
                       qmp.property("astra-display-submissions") !=
                       qmp.property("astra-display-completions")) and \
                        time.monotonic() < resize_deadline:
                    time.sleep(0.01)
            except (BrokenPipeError, ConnectionError) as error:
                time.sleep(0.05)
                raise RuntimeError(
                    "Terminal resize terminated machine exit=%s serial=%s" %
                    (machine.poll(), serial[-100:])) from error
            resized_batches = qmp.property(
                "astra-display-render-batches")
            resized_glyphs = qmp.property("astra-display-glyph-commands")
            resize_submit_cycle = qmp.property(
                "astra-display-submit-cycle")
            resize_completion_cycle = qmp.property(
                "astra-display-completion-cycle")
            resize_collect_cycle = qmp.property(
                "astra-display-collect-cycle")
            resize_cycles = resize_collect_cycle - resize_submit_cycle
            resize_elapsed = time.monotonic() - resize_started
            if resized_batches <= resize_batches or \
                    resized_glyphs <= resize_glyphs or \
                    qmp.property("astra-display-submissions") != \
                    qmp.property("astra-display-completions"):
                raise RuntimeError(
                    "Terminal southeast resize failed: batches=%d/%d "
                    "glyphs=%d/%d requests=%d/%d" %
                    (resize_batches, resized_batches, resize_glyphs,
                     resized_glyphs,
                     qmp.property("astra-display-submissions"),
                     qmp.property("astra-display-completions")))
            if not (resize_submit_cycle < resize_completion_cycle <=
                    resize_collect_cycle) or \
                    resize_cycles > RESIZE_BUDGET_CYCLES:
                raise RuntimeError(
                    "Terminal resize render took %d cycles; budget %d" %
                    (resize_cycles, RESIZE_BUDGET_CYCLES))
            # The fixture opens at (180,90), 840 pixels wide, with the system
            # theme's 2-pixel frame, 4-pixel spacing and 20-pixel gadgets.
            maximize_batches = resized_batches
            qmp.move(1088, 105)
            qmp.click()
            maximize_deadline = time.monotonic() + 3.0
            while (qmp.property("astra-display-render-batches") <
                   maximize_batches + 3 or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < maximize_deadline:
                time.sleep(0.01)
            maximized_batches = qmp.property(
                "astra-display-render-batches")
            if maximized_batches < maximize_batches + 3 or \
                    qmp.property("astra-display-submissions") != \
                    qmp.property("astra-display-completions"):
                raise RuntimeError(
                    "maximize route batches=%d requests=%d/%d" %
                    (maximized_batches,
                     qmp.property("astra-display-submissions"),
                     qmp.property("astra-display-completions")))
            qmp.move(1244, 49)
            qmp.click()
            restore_deadline = time.monotonic() + 3.0
            while (qmp.property("astra-display-render-batches") <
                   maximized_batches + 1 or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < restore_deadline:
                time.sleep(0.01)
            restored_batches = qmp.property(
                "astra-display-render-batches")
            if restored_batches < maximized_batches + 1 or \
                    qmp.property("astra-display-submissions") != \
                    qmp.property("astra-display-completions"):
                raise RuntimeError(
                    "restore route batches=%d/%d cursor=%d,%d "
                    "requests=%d/%d" %
                    (maximized_batches, restored_batches,
                     qmp.property("astra-display-cursor-x"),
                     qmp.property("astra-display-cursor-y"),
                     qmp.property("astra-display-submissions"),
                     qmp.property("astra-display-completions")))
            second_batches = restored_batches
            second_glyphs = qmp.property("astra-display-glyph-commands")
            qmp.move(70, 90)
            qmp.click()
            time.sleep(0.08)
            qmp.click()
            second_deadline = time.monotonic() + 5.0
            while (qmp.property("astra-display-render-batches") <
                   second_batches + 2 or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < second_deadline:
                time.sleep(0.01)
            if qmp.property("astra-display-render-batches") < \
                    second_batches + 2 or \
                    qmp.property("astra-display-glyph-commands") <= \
                    second_glyphs:
                raise RuntimeError(
                    "concurrent Terminal launch failed: batches=%d/%d "
                    "glyphs=%d/%d" %
                    (second_batches,
                     qmp.property("astra-display-render-batches"),
                     second_glyphs,
                     qmp.property("astra-display-glyph-commands")))
            second_batches = qmp.property("astra-display-render-batches")
            qmp.move(1008, 105)
            time.sleep(0.1)
            close_hover_batches = qmp.property(
                "astra-display-render-batches")
            if close_hover_batches <= second_batches:
                raise RuntimeError("close gadget did not enter hover state")
            qmp.click()
            time.sleep(0.2)
            second_close_deadline = time.monotonic() + 3.0
            while (qmp.property("astra-display-render-batches") <
                   close_hover_batches + 2 or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < second_close_deadline:
                time.sleep(0.01)
            if qmp.property("astra-display-render-batches") < \
                    close_hover_batches + 2:
                raise RuntimeError("second Terminal did not close")
            closed_batches = qmp.property("astra-display-render-batches")
            relaunch_batches = closed_batches
            relaunch_glyphs = qmp.property("astra-display-glyph-commands")
            qmp.move(70, 90)
            qmp.click()
            time.sleep(0.08)
            qmp.click()
            relaunch_deadline = time.monotonic() + 5.0
            while (qmp.property("astra-display-render-batches") <
                   relaunch_batches + 1 or
                   qmp.property("astra-display-glyph-commands") <=
                   relaunch_glyphs or
                   qmp.property("astra-display-submissions") !=
                   qmp.property("astra-display-completions")) and \
                    time.monotonic() < relaunch_deadline:
                time.sleep(0.01)
            if qmp.property("astra-display-render-batches") <= \
                    relaunch_batches or \
                    qmp.property("astra-display-glyph-commands") <= \
                    relaunch_glyphs or \
                    qmp.property("astra-display-submissions") != \
                    qmp.property("astra-display-completions"):
                raise RuntimeError(
                    "Terminal did not relaunch after close: batches=%d/%d "
                    "glyphs=%d/%d serial=%s" %
                    (relaunch_batches,
                     qmp.property("astra-display-render-batches"),
                     relaunch_glyphs,
                     qmp.property("astra-display-glyph-commands"),
                     serial[-100:]))
            commands = qmp.property("astra-display-render-commands")
            fills = qmp.property("astra-display-fill-commands")
            blits = qmp.property("astra-display-blit-commands")
            glyphs = qmp.property("astra-display-glyph-commands")
            submissions = qmp.property("astra-display-submissions")
            batches = qmp.property("astra-display-render-batches")
            present_cycles = collect_cycle - submit_cycle
            if not (submit_cycle < completion_cycle <= collect_cycle):
                raise RuntimeError("display cycle order invalid: %d %d %d" %
                                   (submit_cycle, completion_cycle,
                                    collect_cycle))
            if present_cycles > PRESENT_BUDGET_CYCLES:
                raise RuntimeError("display present took %d cycles; budget %d" %
                                   (present_cycles, PRESENT_BUDGET_CYCLES))
            time.sleep(0.1)
            if machine.poll() is not None:
                raise RuntimeError("machine exited after display startup: %s" %
                                   serial[-8:])
            print("ASTRA DISPLAY PASS fences=%d batches=%d commands=%d "
                  "fills=%d blits=%d glyphs=%d generation=%d cycles=%d/%d "
                  "pointer=%d/%d cpu=%.3fMHz boot=%.3fs launch=%.3fs "
                  "resize=%d/%dcycles %.3fs" %
                  (submissions,
                   batches, commands, fills, blits, glyphs, generation,
                   present_cycles, PRESENT_BUDGET_CYCLES, pointer_cycles,
                   POINTER_BUDGET_CYCLES, measured_hz / 1000000.0,
                   elapsed, launch_elapsed, resize_cycles,
                   RESIZE_BUDGET_CYCLES, resize_elapsed))
            return 0
        finally:
            machine.kill()
            machine.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    arguments = parser.parse_args()
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline)


if __name__ == "__main__":
    sys.exit(main())
