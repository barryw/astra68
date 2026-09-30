#!/usr/bin/env python3
"""Upstream SDL2 loopwave, heard through a stand-in host audio daemon.

loopwave decodes test/sample.wav (MS-ADPCM, mono, 22050 Hz) in SDL and plays
it in a loop through pcm.library.2, the media service and QEMU's host audio
provider. The Linux audio host converts and resamples every stream, so
SDL's device takes loopwave's own format (S16BE mono 22050 Hz) and the
MC68040 converts nothing. The provider talks to the Linux daemon's socket,
which here is a stand-in: it keeps the daemon's per-voice 4096-frame queues
and drains each at its own rate, so backpressure is the daemon's, and it
records every frame each voice is given.

The gate decodes sample.wav itself (bit-exact with SDL_wave.c and ffmpeg)
and requires loopwave's stream to be exactly that, looped past a loop
boundary, in that format. Queue underruns (the daemon's software gaps) are
reported; QEMU time on beast is not physical DE25 throughput.

--heard FILE checks a DE25 capture of the physical daemon's final mix
(fpga/de25/linux/audio_monitor.py): 48 kHz after the host's resampler, so
every 0.5 s window must correlate with sample.wav resampled here, left
equal right, with no drift beyond a few frames. A dropped, repeated or
byte-swapped packet, an underrun's inserted silence, or a wrong rate fails
it.
"""

import argparse
import importlib.util
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

import numpy


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)

# sw/include/astra/audio_host.h, host.h, pcm_format.h, status.h
MAGIC, VERSION = 0x41554431, 2
REQUEST = struct.Struct("=6I")
REPLY = struct.Struct("=8I")
PACKET_FRAMES, QUEUE_FRAMES = 1024, 4096
OPEN, WRITE, GAIN, STATUS, CLOSE, FINISH, PAUSE, CLEAR = range(1, 9)
# pcm_format.h: a format word is encoding | channels << 8 | rate << 12.
S24LE, S16BE = 1, 2
ENCODING_BYTES = {1: 3, 2: 2, 3: 1, 4: 1, 5: 2, 6: 2, 7: 2, 8: 4, 9: 4,
                  10: 4, 11: 4}
MAX_FRAME_BYTES, RATE_MIN, RATE_MAX = 8, 8000, 192000


def frame_bytes(format_):
    encoding, channels, rate = format_ & 0xFF, (format_ >> 8) & 0xF, \
        format_ >> 12
    if (encoding not in ENCODING_BYTES or not 1 <= channels <= 2 or
            not RATE_MIN <= rate <= RATE_MAX):
        return 0
    return ENCODING_BYTES[encoding] * channels


OK, PROTOCOL, INVALID, BAD_HANDLE, UNSUPPORTED, BUSY = 0, 1, 8, 9, 13, 14
RATE = 48000


class Voice:
    def __init__(self, handle, format_):
        self.handle = handle
        self.frame_bytes = frame_bytes(format_)
        self.format = format_
        self.encoding = format_ & 0xFF
        self.channels = (format_ >> 8) & 0xF
        self.rate = format_ >> 12
        self.queued = 0.0
        self.paused = False
        self.finished = False
        self.written = bytearray()
        self.gaps = 0


class AudioHost:
    """The Linux audio daemon's socket protocol, with a clock for a sink."""

    def __init__(self, path):
        self.lock = threading.Lock()
        self.voices = []
        self.next_handle = 0
        self.clock = time.monotonic()
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.listener.bind(path)
        self.listener.listen(8)
        self.stopping = False
        threading.Thread(target=self.accept, daemon=True).start()

    def drain(self):
        now = time.monotonic()
        elapsed = now - self.clock
        self.clock = now
        for voice in self.voices:
            # The host resamples each voice to the sink, so its queue
            # drains at the voice's own rate.
            frames = elapsed * voice.rate
            if voice.paused or voice.queued == 0:
                continue
            if frames > voice.queued and not voice.finished:
                voice.gaps += 1
            voice.queued = max(0.0, voice.queued - frames)

    def accept(self):
        while not self.stopping:
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(client,),
                             daemon=True).start()

    def serve(self, client):
        mine = {}
        while True:
            try:
                packet = client.recv(REQUEST.size +
                                     PACKET_FRAMES * MAX_FRAME_BYTES)
            except OSError:
                return
            if not packet:
                return
            with self.lock:
                self.drain()
                reply = self.execute(mine, packet)
            client.send(REPLY.pack(*reply))

    def execute(self, mine, packet):
        reply = [MAGIC, OK, 0, 0, 0, 0, 0, 0]
        if len(packet) < REQUEST.size:
            reply[1] = PROTOCOL
            return reply
        magic, version, operation, handle, value, length = \
            REQUEST.unpack_from(packet)
        data = packet[REQUEST.size:]
        if (magic != MAGIC or version != VERSION or length != len(data)):
            reply[1] = PROTOCOL
            return reply
        voice = mine.get(handle)
        if operation == OPEN:
            if handle != 0 or not frame_bytes(value) or data:
                reply[1] = INVALID
                return reply
            self.next_handle += 1
            voice = Voice(self.next_handle, value)
            mine[voice.handle] = voice
            self.voices.append(voice)
            reply[2] = voice.handle
        elif operation == STATUS and handle == 0:
            pass
        elif voice is None:
            reply[1] = BAD_HANDLE
        elif operation == WRITE:
            frames = len(data) // voice.frame_bytes
            if (voice.finished or not data or
                    len(data) % voice.frame_bytes != 0 or
                    frames > PACKET_FRAMES):
                reply[1] = INVALID
            elif frames > QUEUE_FRAMES - int(voice.queued + 0.999):
                reply[1] = BUSY
            else:
                voice.written += data
                voice.queued += frames
                reply[3] = int(voice.queued + 0.999)
        elif operation == STATUS:
            reply[3] = int(voice.queued + 0.999)
        elif operation == FINISH:
            voice.finished = True
        elif operation == PAUSE:
            voice.paused = bool(value)
        elif operation == CLEAR:
            voice.queued = 0.0
        elif operation == CLOSE:
            del mine[handle]
            voice.queued = 0.0
        elif operation != GAIN:
            reply[1] = UNSUPPORTED
        return reply

    def close(self):
        self.stopping = True
        self.listener.close()


def ms_adpcm_mono(path):
    """sample.wav's samples, decoded the way the format defines them."""
    with open(path, "rb") as handle:
        data = handle.read()
    chunks, at = {}, 12
    while at + 8 <= len(data):
        name, size = struct.unpack_from("<4sI", data, at)
        chunks[name] = data[at + 8:at + 8 + size]
        at += 8 + size + (size & 1)
    fmt = chunks[b"fmt "]
    tag, channels, rate, _, block, _, _, per_block, count = \
        struct.unpack_from("<HHIIHHHHH", fmt)
    if tag != 2 or channels != 1:
        raise RuntimeError("sample.wav is not mono MS-ADPCM")
    coefficients = struct.unpack_from("<%dh" % (2 * count), fmt, 22)
    adapt = (230, 230, 230, 230, 307, 409, 512, 614,
             768, 614, 512, 409, 307, 230, 230, 230)
    samples = []
    payload = chunks[b"data"]
    for base in range(0, len(payload) - block + 1, block):
        predictor, delta, first, second = \
            struct.unpack_from("<BHhh", payload, base)
        c1 = coefficients[2 * predictor]
        c2 = coefficients[2 * predictor + 1]
        s1, s2 = first, second
        samples += (s2, s1)
        for byte in payload[base + 7:base + block]:
            for nibble in (byte >> 4, byte & 15):
                signed = nibble - 16 if nibble & 8 else nibble
                # SDL_wave.c's arithmetic: C division, which truncates.
                product = s1 * c1 + s2 * c2
                value = (abs(product) // 256) * (1 if product >= 0 else -1)
                value = max(-32768, min(32767, value + signed * delta))
                s2, s1 = s1, value
                samples.append(value)
                delta = min(65535, max(16, (adapt[nibble] * delta) // 256))
    if len(samples) % per_block != 0:
        raise RuntimeError("blocks are not %d samples each" % per_block)
    return numpy.array(samples, dtype=numpy.float64), rate


LOOPWAVE = S16BE | 1 << 8 | 22050 << 12  # sample.wav, as SDL decodes it


def sounding(voice):
    """Source frames from the first non-silent one on."""
    samples = numpy.frombuffer(bytes(voice.written), dtype=">i2")
    sound = numpy.flatnonzero(samples)
    return (len(samples) - sound[0]) // voice.channels if len(sound) else 0


def verify_stream(voice, sample, seconds):
    """loopwave's stream, exactly: the host converts, so SDL sends the
    decoded file unchanged, looped, after the silence it plays while the
    device is paused."""
    heard = numpy.frombuffer(bytes(voice.written), dtype=">i2")
    sound = numpy.flatnonzero(heard)
    if not len(sound):
        raise RuntimeError("loopwave's stream is silent")
    heard = heard[sound[0]:]
    need = int(len(sample) + seconds * voice.rate)
    if len(heard) < need:
        raise RuntimeError("only %d frames arrived; %d cover a loop" %
                           (len(heard), need))
    looped = numpy.tile(sample, need // len(sample) + 2)[:len(heard)]
    wrong = numpy.flatnonzero(heard != looped)
    if len(wrong):
        raise RuntimeError("frame %d of loopwave's stream is %d, sample.wav "
                           "says %d" % (wrong[0], heard[wrong[0]],
                                        looped[wrong[0]]))
    return len(heard)


def verify_mix(pcm, sample, rate, seconds):
    """The host's 48 kHz stereo mix (S16BE bytes) against sample.wav,
    resampled here: every 0.5 s window must correlate, left equal right."""
    frames = numpy.frombuffer(pcm, dtype=">i2")
    frames = frames.reshape(-1, 2)
    if not numpy.array_equal(frames[:, 0], frames[:, 1]):
        raise RuntimeError("left and right differ for a mono source")
    heard = frames[:, 0].astype(numpy.float64)
    # SDL plays silence until loopwave unpauses the device.
    sound = numpy.flatnonzero(heard)
    if not len(sound):
        raise RuntimeError("loopwave's stream is silent")
    heard = heard[sound[0]:]
    loop = len(sample) * RATE / rate
    need = int(loop + seconds * RATE)
    if len(heard) < need:
        raise RuntimeError("only %d frames arrived; %d cover a loop" %
                           (len(heard), need))
    heard = heard[:need]
    # The reference: the decoded loop, repeated, at the device rate.
    source = numpy.tile(sample, int(need / loop) + 2)
    offset = RATE  # reference frames before the sound starts, for a lag
    positions = (numpy.arange(need + 2 * RATE) - offset) * (rate / RATE)
    reference = numpy.interp(positions, numpy.arange(len(source)), source,
                             left=0.0)

    def score(start, lag, length):
        at = offset + start + lag
        return numpy.corrcoef(heard[start:start + length],
                              reference[at:at + length])[0, 1]

    # The lag may creep by a frame or two between windows (the resamplers
    # differ); a lost or repeated packet is a jump of hundreds.
    window = RATE // 2
    lag = max(range(-64, 65), key=lambda lag: score(window, lag, window))
    first, worst, gain = lag, 1.0, []
    for start in range(window, need - window + 1, window):
        best = max(range(lag - 4, lag + 5),
                   key=lambda candidate: score(start, candidate, window))
        correlation = score(start, best, window)
        if correlation < 0.97:
            raise RuntimeError("heard audio departs from sample.wav at "
                               "%.1f s (correlation %.3f, lag %d)" %
                               (start / RATE, correlation, best))
        worst = min(worst, correlation)
        lag = best
        at = offset + start + lag
        gain.append(numpy.std(heard[start:start + window]) /
                    numpy.std(reference[at:at + window]))
    drift = (lag - first) / (start - window) * 1e6
    if abs(drift) > 200:
        raise RuntimeError("playback rate is off by %.0f ppm" % drift)
    level = float(numpy.median(gain))
    if not 0.9 <= level <= 1.1:
        raise RuntimeError("level is %.2f of sample.wav's" % level)
    return worst, drift, len(frames)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", nargs="?")
    parser.add_argument("rom", nargs="?")
    parser.add_argument("image", nargs="?")
    parser.add_argument("--wav", required=True,
                        help="the sample.wav loopwave's bundle carries")
    parser.add_argument("--seconds", type=float, default=2.0,
                        help="audio past the first loop boundary to check")
    parser.add_argument("--save", help="write loopwave's stream here "
                        "(raw 48 kHz stereo S16BE)")
    parser.add_argument("--profile", help="profile the guest while it "
                        "plays (QEMU must be the host-profile build; report "
                        "with tools/astra-prof report)")
    parser.add_argument("--heard", help="check this DE25 capture instead "
                        "of running QEMU (fpga/de25/linux/audio_monitor.py: "
                        "raw 48 kHz stereo S16LE)")
    arguments = parser.parse_args()
    sample, rate = ms_adpcm_mono(arguments.wav)
    if arguments.heard:
        # The board's monitor tap is the final mix, so an underrun shows as
        # the daemon's inserted silence and fails the correlation.
        with open(arguments.heard, "rb") as handle:
            pcm = numpy.frombuffer(handle.read(),
                                   dtype="<i2").astype(">i2").tobytes()
        worst, drift, frames = verify_mix(pcm, sample, rate,
                                          arguments.seconds)
        print("SDL upstream loopwave DE25: PASS (%d frames; sample.wav "
              "looped, worst 0.5 s correlation %.3f, rate %+.0f ppm)" %
              (frames, worst, drift))
        return
    if not (arguments.qemu and arguments.rom and arguments.image):
        parser.error("qemu, rom and image are required without --heard")
    loop_seconds = len(sample) / rate
    with tempfile.TemporaryDirectory(prefix="astra-sdl-audio-") as work:
        image = os.path.join(work, "test.img")
        shutil.copyfile(arguments.image, image)
        host = AudioHost(os.path.join(work, "audio.sock"))
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = os.path.join(work,
                                                             "audio.sock")
        extra, control = [], os.path.join(work, "profile.sock")
        profile = [sys.executable,
                   os.path.join(HERE, "..", "..", "tools", "astra-prof"),
                   "control", control]
        if arguments.profile:
            plugin = os.path.join(os.path.dirname(arguments.qemu),
                                  "contrib/plugins/libastra_profile.so")
            if os.path.exists(arguments.profile):
                os.unlink(arguments.profile)
            extra = ["-plugin", "%s,output=%s,control=%s,label=audio" %
                     (plugin, os.path.abspath(arguments.profile), control)]
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work, extra_args=extra)
        profiling = False
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting: %r" %
                                   machine.recent_serial(40))
            deadline = time.monotonic() + loop_seconds * 3 + 90
            need = (loop_seconds + arguments.seconds) * rate
            while True:
                with host.lock:
                    host.drain()
                    # The desktop's startup chime is a voice too; loopwave's
                    # is the one still open and longest.
                    voices = sorted(host.voices,
                                    key=lambda v: len(v.written))
                    heard = sounding(voices[-1]) if voices else 0
                if heard >= need:
                    break
                if arguments.profile and heard and not profiling:
                    subprocess.run(profile + ["start", "audio"], check=True)
                    profiling, profiled = True, (time.monotonic(), heard)
                said = "\n".join(machine.said(0)[0])
                if "ERROR:" in said:
                    raise RuntimeError("loopwave failed: %r" %
                                       machine.said(0)[0][-20:])
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        "loopwave gave %d of %d frames: %r" %
                        (heard, need, machine.said(0)[0][-20:]))
                time.sleep(0.5)
            if profiling:
                subprocess.run(profile + ["stop"], check=True)
                print("profiled %.1f s, %d frames heard" %
                      (time.monotonic() - profiled[0], heard - profiled[1]))
            said = "\n".join(machine.said(0)[0])
            if "Using audio driver: astra" not in said:
                raise RuntimeError("loopwave did not use the Astra driver")
            with host.lock:
                voice = max(host.voices, key=lambda v: len(v.written))
                # SDL must hand the file over as it decoded it: the host
                # converts, the MC68040 does not.
                if voice.format != LOOPWAVE:
                    raise RuntimeError("loopwave's stream is format %#x, "
                                       "not %#x" % (voice.format, LOOPWAVE))
                if arguments.save:
                    with open(arguments.save, "wb") as handle:
                        handle.write(voice.written)
                frames = verify_stream(voice, sample.astype(numpy.int16),
                                       arguments.seconds)
                gaps = voice.gaps
            print("SDL upstream loopwave QEMU: PASS (%d frames of "
                  "S16BE mono 22050 Hz, bit-exact sample.wav looped; %d "
                  "queue underruns)" % (frames, gaps))
        finally:
            machine.close()
            host.close()


if __name__ == "__main__":
    main()
