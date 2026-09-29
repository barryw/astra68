#!/usr/bin/env python3
"""Open every installed application from the running desktop.

Boots the full system image -- desktop, storage, network, audio, every
service -- and double-clicks each application icon where the desktop says it
put it. Each application must be launched by the supervisor under its launch
ceiling, draw through the display, and leave the desktop and every other
application running. Nothing here uses a test-only startup manifest.
"""

import argparse
import importlib.util
import os
import shutil
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location(
    "terminal_gate", os.path.join(os.path.dirname(__file__),
                                  "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)

# Bundles the system image ships, other than Terminal (the terminal gate's).
APPLICATIONS = ("/apps/InterfaceGallery.app", "/apps/SDLTestDraw2.app")


def display_count(machine, name):
    return machine.qmp.execute(
        "qom-get", {"path": "/machine", "property": "astra-display-" + name})


def open_application(machine, bundle, deadline):
    before = machine.sequence()
    submissions = display_count(machine, "submissions")
    machine.qmp.double_click(*machine.desktop_icon(bundle))
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        lines, _ = machine.said(before)
        refused = [line for line in lines
                   if "application launch refused" in line or
                   "desktop launch" in line]
        if refused:
            raise RuntimeError("%s did not launch: %s" % (bundle, refused))
        launched = any(("launch " + bundle) in line for line in lines)
        if launched and display_count(machine, "submissions") > submissions:
            return
        time.sleep(0.25)
    raise RuntimeError("%s launched no drawing within %.0fs: %s" %
                       (bundle, deadline, machine.said(before)[0][-20:]))


# TestDraw2's 640x480 window opens centred; with the system theme's frame,
# (640, 300) is its frame origin, the title bar's centre line is 15 pixels
# down and the close gadget's centre 12 pixels in from the right edge.
DRAW2_FRAME = (640, 300, 640)
TITLE_Y = 15
CLOSE_INSET = 12


def wait_quiet(machine, what, seconds=0.5, deadline=10.0):
    end = time.monotonic() + deadline
    last = None
    since = time.monotonic()
    while time.monotonic() < end:
        now = (display_count(machine, "submissions"),
               display_count(machine, "completions"))
        if now != last:
            last, since = now, time.monotonic()
        elif time.monotonic() - since >= seconds:
            return
        time.sleep(0.05)
    raise RuntimeError("the display never went quiet (%s)" % what)


def require_pointer(machine, points):
    for x, y in points:
        before = display_count(machine, "cursor-updates")
        machine.qmp.point(x, y)
        end = time.monotonic() + 5.0
        while display_count(machine, "cursor-updates") == before or \
                (display_count(machine, "cursor-x"),
                 display_count(machine, "cursor-y")) != (x, y):
            if time.monotonic() >= end:
                raise RuntimeError(
                    "pointer stopped: cursor %s visible=%d, pointer at %s; "
                    "last trace %s" % (
                        (display_count(machine, "cursor-x"),
                         display_count(machine, "cursor-y")),
                        display_count(machine, "cursor-visible"), (x, y),
                        machine.said()[0][-12:]))
            time.sleep(0.05)


def drag_close_and_check_pointer(machine):
    """What a person does: drag TestDraw2 by its title bar, close it with
    its close gadget, and keep using the mouse."""
    left, top, width = DRAW2_FRAME
    machine.qmp.point(left + 80, top + TITLE_Y)
    time.sleep(0.2)
    machine.qmp.button(True)
    for step in range(1, 7):
        machine.qmp.point(left + 80 + step * 10, top + TITLE_Y + step * 5)
        time.sleep(0.05)
    machine.qmp.button(False)
    left, top = left + 60, top + 30
    machine.qmp.point(left + width - CLOSE_INSET, top + TITLE_Y)
    time.sleep(0.3)
    machine.qmp.button(True)
    time.sleep(0.05)
    machine.qmp.button(False)
    wait_quiet(machine, "TestDraw2 still drawing: its close gadget missed")
    # The pointer is still over the closed window's gadget; moving it must
    # reach the cursor, and the desktop must keep answering.
    require_pointer(machine, ((left + width - CLOSE_INSET + 3, top + TITLE_Y),
                              (200, 200), (1500, 700), (960, 540)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--deadline", type=float, default=60.0)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-desktop-apps-") as work:
        image = os.path.join(work, "system.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting")
            for bundle in APPLICATIONS:
                open_application(machine, bundle, arguments.deadline)
            # Everything opened is still running and the display kept up.
            if machine.process.poll() is not None:
                raise RuntimeError("QEMU exited")
            # TestDraw2 animates without end: the display must keep
            # completing its frames with at most one request in flight.
            # Submissions are read before completions: drawing continues
            # between the two reads, and read the other way round a fast
            # display looks behind by however much it submitted meanwhile.
            first = display_count(machine, "completions")
            time.sleep(2.0)
            submitted = display_count(machine, "submissions")
            completed = display_count(machine, "completions")
            if completed <= first:
                raise RuntimeError("drawing stopped: %d completions in 2s" %
                                   (completed - first))
            if submitted - completed > 1:
                raise RuntimeError("display fell behind: %d requests pending"
                                   % (submitted - completed))
            drag_close_and_check_pointer(machine)
            print("ASTRA DESKTOP APPS PASS %d applications, %d frames/2s"
                  % (len(APPLICATIONS), completed - first))
        finally:
            machine.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print("ASTRA DESKTOP APPS FAIL: %s" % error)
        sys.exit(1)
