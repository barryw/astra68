#!/usr/bin/env python3
"""Boot the SDL_mixer probe and require its real mixed-channel result."""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import time


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)
spec = importlib.util.spec_from_file_location(
    "astra_sdl_audio_gate", os.path.join(HERE, "test-sdl-audio.py"))
audio_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio_gate)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    if os.geteuid() != 0:
        environment = ["ASTRA_TEST_QEMU_UID=%d" % os.getuid(),
                       "ASTRA_TEST_QEMU_GID=%d" % os.getgid()]
        worker = os.environ.get("ASTRA_CODEC_WORKER_PATH")
        if worker:
            environment.append("ASTRA_CODEC_WORKER_PATH=" + worker)
        os.execvp("sudo", ["sudo", "-n", "env", *environment,
                            sys.executable, os.path.abspath(__file__),
                            *sys.argv[1:]])
    with tempfile.TemporaryDirectory(prefix="astra-sdl-mixer-") as temporary:
        image = os.path.join(temporary, "test.img")
        shutil.copyfile(arguments.image, image)
        host = audio_gate.AudioHost(os.path.join(temporary, "audio.sock"),
                                    synthesis=False)
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = os.path.join(
            temporary, "audio.sock")
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, temporary)
        try:
            deadline = time.monotonic() + 120
            probe_pid = None
            progress = []
            while time.monotonic() < deadline:
                try:
                    trace = machine.trace_with_syslog()
                except terminal_gate.trace_decode.TraceError:
                    time.sleep(0.1)
                    continue
                for index, line in enumerate(trace):
                    if "SDL_MIXER_" in line and line not in progress:
                        progress.append(line)
                    match = re.search(r"info\s+([0-9a-fA-F]{8})/0 "
                                      r"/apps/SDLMixerProbe.app", line)
                    if match:
                        probe_pid = int(match.group(1), 16)
                    match = re.search(r"process_exit\s+0x([0-9a-fA-F]{8}) "
                                      r"0x([0-9a-fA-F]{8})", line)
                    if match and probe_pid == int(match.group(1), 16):
                        if int(match.group(2), 16) != 0:
                            raise RuntimeError("SDL_mixer probe failed: "
                                               "progress=%r requests=%r voices=%r context=%r" %
                                               (progress[-15:], host.requests,
                                                [(v.format, len(v.written))
                                                 for v in host.voices], trace[max(
                                                   0, index - 35):index + 8]))
                        audible = [voice for voice in host.voices
                                   if len(voice.written) >= 4096 and
                                   any(voice.written)]
                        music_logs = [entry for entry in trace
                                      if "Astra Codec music: underflows=" in entry]
                        if not audible or not any(
                                "SDL_MIXER_OGG_START" in line
                                for line in progress) or not any(
                                "underflows=0 " in entry
                                for entry in music_logs):
                            raise RuntimeError("SDL_mixer PCM or Ogg queue gate failed: "
                                               "requests=%r voices=%r music=%r "
                                               "progress=%r trace=%r" % (
                                                   host.requests,
                                                   [(v.format, len(v.written))
                                                    for v in host.voices],
                                                   music_logs,
                                                   progress[-15:],
                                                   trace[max(0, index - 35):
                                                         index + 8]))
                        print("SDL_mixer QEMU: PASS (WAV, MP3, OGG, FLAC, "
                              "invalid input, 16 channels, host PCM)")
                        return
                time.sleep(0.1)
            trace = machine.trace_with_syslog()
            registers = machine.qmp.monitor("info registers")
            raise RuntimeError("SDL_mixer probe timed out: progress=%r "
                               "registers=%r tail=%r" %
                               (progress, registers[-1600:], trace[-20:]))
        finally:
            machine.close()
            host.close()


if __name__ == "__main__":
    main()
