#!/usr/bin/env python3
"""Chocolate Doom 3.1.1, unmodified, started as an application.

The image holds ChocolateDoom.app (the game, the shareware doom1.wad and
nothing else Doom needs outside Astra's shared libraries) and starts it with
no arguments, as the desktop does. Doom must find its IWAD from its bundle
(the application's working directory), open a window through the hardware
renderer, and play its title and demos: the stand-in display helper must
receive render-only batches steadily, and the stand-in audio daemon a
44.1 kHz stereo S16BE voice carrying sound. No process may fault.

Then it is played from the keyboard, as a person would: Escape opens the
menu over the demo, Enter starts a new game (episode, skill), Escape returns
to the menu from play, Up and Enter choose Quit Game and `y` confirms. Doom
must then show ENDOOM and wait for a key, and that key must end it with
status 0. Every step depends on the one before, so a key that is lost or
misread leaves Doom running and fails the gate.
"""

import argparse
import importlib.util
import mmap
import os
import shutil
import sys
import tempfile
import threading
import time

import numpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal_gate = load("astra_terminal_gate", "test-terminal.py")
mailbox_gate = load("astra_display_mailbox_gate", "test-display-mailbox.py")
audio_gate = load("astra_sdl_audio_gate", "test-sdl-audio.py")
runtime_gate = load("astra_sdl_runtime_gate", "test-sdl-runtime.py")

PRESENT_RENDER_BATCH = 3
DOOM_AUDIO = audio_gate.S16BE | 2 << 8 | 44100 << 12


class Batches:
    def __init__(self):
        self.lock = threading.Lock()
        self.times = []

    def observe(self, operation, frame_bytes):
        if operation == PRESENT_RENDER_BATCH:
            with self.lock:
                self.times.append(time.monotonic())

    def rate(self, seconds):
        now = time.monotonic()
        with self.lock:
            return sum(1 for t in self.times if t > now - seconds) / seconds


def story(log):
    """What the machine logged, kept across polls (remote desktop retries
    wrap the ring under QEMU, which has no helper for it), less the loader's
    progress lines, and the process exits."""
    lines = [line for _, line in log.lines()
             if "remote-desktop" not in line and "dynamic loader" not in line]
    return lines[-40:], log.exits[-6:]


def faults(machine):
    return [line for line in machine.trace()
            if "process_exit" in line and
            int(line.split()[-2], 16) in (3, 4)]


# (qcode, seconds to let Doom act on it: menus wipe, a new game loads).
PLAY = (("esc", 2), ("ret", 1.5), ("ret", 1.5), ("ret", 5),
        ("esc", 2), ("up", 1), ("ret", 1.5), ("y", 4))


def wait_exit(log, after, seconds):
    """The first clean exit after trace sequence @p after, or None."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        log.poll()
        if any(reason in (3, 4) for _, _, reason in log.exits):
            raise RuntimeError("a process faulted: %r" % log.exits[-6:])
        for at, status, reason in log.exits:
            if at > after and (status, reason) == (0, 1):
                return at
        time.sleep(0.25)
    return None


def play_and_quit(machine, log):
    log.poll()
    start = log.newest
    for qcode, settle in PLAY:
        machine.qmp.key(qcode)
        time.sleep(settle)
    # ENDOOM waits for a key: Doom is still alive.
    if wait_exit(log, start, 2) is not None:
        raise RuntimeError("Doom exited without showing ENDOOM: %r" %
                           (story(log),))
    log.poll()
    before = log.newest
    machine.qmp.key("spc")
    if wait_exit(log, before, 20) is None:
        raise RuntimeError("Doom did not quit from its menu and ENDOOM: %r"
                           % (story(log),))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--seconds", type=float, default=30.0)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-doom-") as work:
        image = os.path.join(work, "test.img")
        shutil.copyfile(arguments.image, image)
        batches = Batches()
        mailbox = os.path.join(work, "mailbox.bin")
        mailbox_gate.create_mailbox(mailbox)
        payload_file = open(mailbox_gate.payload_path(mailbox), "r+b")
        payload = mmap.mmap(payload_file.fileno(), 0)
        stream = open(mailbox, "r+b")
        view = mmap.mmap(stream.fileno(), mailbox_gate.HEADER_BYTES)
        helper = mailbox_gate.Helper(view, observe=batches.observe)
        helper.thread.start()
        os.environ["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
        os.environ["ASTRA_DISPLAY_PAYLOAD_PATH"] = \
            mailbox_gate.payload_path(mailbox)
        host = audio_gate.AudioHost(os.path.join(work, "audio.sock"))
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = os.path.join(work,
                                                             "audio.sock")
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work)
        log = runtime_gate.Log(machine, strict=False)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting: %r" %
                                   machine.recent_serial(40))
            deadline = time.monotonic() + 120
            while len(batches.times) < 50:
                log.poll()
                # Status 0x10 is the remote desktop's retry under QEMU.
                if any(status not in (0, 0x10) and reason == 1
                       for _, status, reason in log.exits):
                    raise RuntimeError("a program exited with an error: %r"
                                       % (story(log),))
                if faults(machine):
                    raise RuntimeError("a process faulted: %r" %
                                       machine.said(0)[0][-30:])
                if time.monotonic() >= deadline:
                    raise RuntimeError("Doom rendered %d batches (host "
                                       "audio requests %r): %r" %
                                       (len(batches.times),
                                        dict(host.requests), story(log)))
                time.sleep(0.1)
            time.sleep(arguments.seconds)
            if faults(machine):
                raise RuntimeError("a process faulted: %r" %
                                   machine.said(0)[0][-30:])
            said = machine.said(0)[0]
            errors = [line for line in said if "ERROR:" in line]
            if errors:
                raise RuntimeError("SDL reported errors: %r" % errors[:5])
            rate = batches.rate(arguments.seconds)
            with host.lock:
                host.drain()
                voices = [v for v in host.voices if v.format == DOOM_AUDIO]
                if not voices:
                    raise RuntimeError("no 44.1 kHz stereo voice: %r" %
                                       [hex(v.format) for v in host.voices])
                samples = numpy.frombuffer(bytes(voices[0].written),
                                           dtype=">i2")
                loud = int(numpy.abs(samples).max()) if len(samples) else 0
                frames = len(samples) // 2
            if loud == 0:
                raise RuntimeError("Doom's audio is silent")
            # Its 11025 Hz effects reach the 44.1 kHz mixer through the
            # host converter, not SDL's soft-float resampler.
            with host.lock:
                conversions = host.conversions
            if conversions == 0:
                raise RuntimeError("Doom converted no sound on the host")
            # Its music is MIDI on the host's SoundFont synthesizer, not
            # OPL on the MC68040: a song played and was heard.
            with host.lock:
                host.drain()
                songs = [m for m in host.midis.values() if m.plays]
                music_frames = sum(m.frames for m in songs)
                music_energy = sum(m.energy for m in songs)
                music_peak = max((m.peak for m in songs), default=0.0)
            if not songs or music_frames == 0 or music_energy == 0.0:
                raise RuntimeError("no MIDI music from the host synthesizer "
                                   "(%d songs, %d frames)" %
                                   (len(songs), music_frames))
            play_and_quit(machine, log)
            if faults(machine):
                raise RuntimeError("a process faulted: %r" %
                                   machine.said(0)[0][-30:])
            print("Chocolate Doom QEMU: PASS (%.1f render batches/s over "
                  "%.0f s; %d audio frames at 44.1 kHz, peak %d; %d effects "
                  "converted on the host; %.1f s of host MIDI music, peak "
                  "%.2f of full scale; played "
                  "from the keyboard, quit through ENDOOM; no faults)"
                  % (rate, arguments.seconds, frames, loud, conversions,
                     music_frames / 48000.0, music_peak))
        finally:
            machine.close()
            host.close()
            helper.stopping = True
            helper.thread.join(timeout=1)
            helper.release()
            view.close()
            payload.close()


if __name__ == "__main__":
    main()
