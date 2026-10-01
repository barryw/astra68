#!/usr/bin/env python3
"""Upstream SDL2 loopwave, heard through a stand-in host audio daemon.

loopwave is a command. The gate installs SDL's sample.wav as
/home/sample.wav, types `loopwave /home/sample.wav` in the Terminal, and ends
it with Ctrl-C: SDL turns SIGINT into SDL_QUIT, so loopwave must close its
PCM stream and exit 0.

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
import collections
import ctypes
import fcntl
import hashlib
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
spec = importlib.util.spec_from_file_location(
    "astra_image", os.path.join(HERE, "astra_image.py"))
astra_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(astra_image)

# sw/include/astra/audio_host.h, host.h, pcm_format.h, status.h
MAGIC, VERSION = 0x41554431, 5
REQUEST = struct.Struct("=8I")
REPLY = struct.Struct("=10I")
PACKET_FRAMES, QUEUE_FRAMES = 1024, 4096
OPEN, WRITE, GAIN, STATUS, CLOSE, FINISH, PAUSE, CLEAR, CONVERT_OPEN, \
    CONVERT, FONT_QUERY, FONT_BEGIN, FONT_DATA, FONT_END, MIDI_OPEN, \
    MIDI_FONT, MIDI_LOAD, MIDI_PLAY, MIDI_STOP, MIDI_STATUS, \
    MIDI_SYSTEM_FONT, FONT_LIST, MIDI_PRESETS, MIDI_EVENTS, \
    MIDI_SET = range(1, 26)
NOT_FOUND, MIDI_FOREVER = 2, 0xFFFFFFFF
CONVERT_END = 1
ROOT = os.path.dirname(os.path.dirname(HERE))


def converter_library(directory):
    """The daemon's own converter (fpga/arty/linux/astra_audio_convert.c),
    built for this host: the stand-in converts exactly as the board does."""
    source = os.path.join(ROOT, "fpga/arty/linux/astra_audio_convert.c")
    library = os.path.join(directory, "astra_audio_convert.so")
    subprocess.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC",
                    "-I" + os.path.join(ROOT, "sw/include"), source, "-lm",
                    "-o", library], check=True)
    loaded = ctypes.CDLL(library)
    loaded.astra_audio_converter_open.restype = ctypes.c_void_p
    loaded.astra_audio_converter_open.argtypes = [ctypes.c_uint32] * 2
    loaded.astra_audio_converter_close.argtypes = [ctypes.c_void_p]
    loaded.astra_audio_converter_write.restype = ctypes.c_uint32
    loaded.astra_audio_converter_write.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    loaded.astra_audio_converter_end.argtypes = [ctypes.c_void_p]
    loaded.astra_audio_converter_ready.restype = ctypes.c_uint32
    loaded.astra_audio_converter_ready.argtypes = [ctypes.c_void_p]
    loaded.astra_audio_converter_read.restype = ctypes.c_uint32
    loaded.astra_audio_converter_read.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    return loaded


def synth_library(directory):
    """The daemon's SoundFont synthesizer (astra_audio_synth.c) with the
    pinned FluidSynth, built for this host."""
    fluidsynth = os.path.join(ROOT, "build/fluidsynth-host")
    archive = os.path.join(fluidsynth, "src/libfluidsynth.a")
    # Gates run side by side; the build script starts with rm -rf.
    os.makedirs(os.path.dirname(fluidsynth), exist_ok=True)
    with open(fluidsynth + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not os.path.exists(archive):
            subprocess.run(["sh", os.path.join(ROOT,
                                               "mk/build-fluidsynth.sh"),
                            "host", fluidsynth], check=True,
                           stdout=subprocess.DEVNULL)
    library = os.path.join(directory, "astra_audio_synth.so")
    source = os.environ.get("FLUIDSYNTH_ROOT",
                            os.path.join(os.path.dirname(ROOT), "fluidsynth"))
    subprocess.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC",
                    "-I" + os.path.join(ROOT, "sw/include"),
                    "-I" + os.path.join(source, "include"),
                    "-I" + os.path.join(fluidsynth, "include"),
                    os.path.join(ROOT, "fpga/arty/linux/astra_audio_synth.c"),
                    archive, "-lstdc++", "-lpthread", "-lm", "-o", library],
                   check=True)
    loaded = ctypes.CDLL(library)
    loaded.astra_audio_synth_open.restype = ctypes.c_void_p
    loaded.astra_audio_synth_open.argtypes = [ctypes.c_uint32]
    for name, arguments in (
            ("close", []), ("add_font", [ctypes.c_char_p]),
            ("load", [ctypes.c_char_p, ctypes.c_uint32]),
            ("play", [ctypes.c_int32]), ("pause", [ctypes.c_int]),
            ("stop", []), ("active", []), ("status", []), ("ready", []),
            ("read", [ctypes.c_void_p, ctypes.c_uint32])):
        function = getattr(loaded, "astra_audio_synth_" + name)
        function.argtypes = [ctypes.c_void_p] + arguments
        function.restype = None if name == "close" else ctypes.c_uint32
    loaded.astra_audio_synth_active.restype = ctypes.c_int
    loaded.astra_audio_synth_report.argtypes = [ctypes.c_void_p,
                                                ctypes.c_void_p]
    loaded.astra_audio_synth_report.restype = None
    loaded.astra_audio_synth_presets.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    loaded.astra_audio_synth_presets.restype = ctypes.c_uint32
    loaded.astra_audio_synth_events.argtypes = [ctypes.c_void_p,
                                                ctypes.c_char_p,
                                                ctypes.c_uint32]
    loaded.astra_audio_synth_events.restype = ctypes.c_uint32
    loaded.astra_audio_synth_set.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                             ctypes.c_uint32]
    loaded.astra_audio_synth_set.restype = ctypes.c_uint32
    loaded.astra_audio_synth_set_sound_fonts.argtypes = [ctypes.c_int]
    loaded.astra_audio_synth_set_sound_fonts.restype = None
    loaded.astra_audio_synth_open_sound_font.argtypes = [ctypes.c_char_p]
    loaded.astra_audio_synth_open_sound_font.restype = ctypes.c_int
    loaded.astra_audio_synth_sound_font_name.argtypes = [ctypes.c_char_p]
    loaded.astra_audio_synth_sound_font_name.restype = ctypes.c_int
    return loaded


class Midi:
    def __init__(self, handle, synth):
        self.handle = handle
        self.synth = synth
        self.song = bytearray()
        self.size = 0
        self.gain = 65536
        self.frames = 0
        self.energy = 0.0
        self.peak = 0.0
        self.plays = 0
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
        self.closed = False


class AudioHost:
    """The Linux audio daemon's socket protocol, with a clock for a sink."""

    def __init__(self, path, hostfs=None):
        work = os.path.dirname(path)
        self.convert = converter_library(work)
        self.synthesis = synth_library(work)
        self.font_directory = os.path.join(work, "fonts")
        os.makedirs(self.font_directory, exist_ok=True)
        # The SOUND volume of the machine's HostFS root (terminal_gate's
        # Machine uses WORK/hostfs), with the shared set the release
        # installs there.
        sound_fonts = os.path.join(hostfs or os.path.join(work, "hostfs"),
                                   "sound", "soundfonts")
        os.makedirs(sound_fonts, exist_ok=True)
        shipped = os.path.join(ROOT,
                               "sw/userspace/services/media/build/soundfonts")
        for name in os.listdir(shipped):
            shutil.copyfile(os.path.join(shipped, name),
                            os.path.join(sound_fonts, name))
        self.sound_fonts = os.open(sound_fonts,
                                   os.O_RDONLY | os.O_DIRECTORY)
        self.synthesis.astra_audio_synth_set_sound_fonts(self.sound_fonts)
        self.uploads = {}
        self.midis = {}
        self.converters = {}
        self.conversions = 0
        self.lock = threading.Lock()
        self.requests = collections.Counter()
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
        # Synth voices are heard at the sink's rate: take what they have.
        for midi in self.midis.values():
            want = min(int(elapsed * RATE), 48000)
            buffer = (ctypes.c_float * (2 * max(want, 1)))()
            got = self.synthesis.astra_audio_synth_read(
                midi.synth, ctypes.cast(buffer, ctypes.c_void_p), want)
            if got:
                left = numpy.frombuffer(buffer, dtype=numpy.float32)[:2 * got]
                midi.frames += got
                midi.energy += float(numpy.square(left).sum())
                midi.peak = max(midi.peak, float(numpy.abs(left).max()))

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
                reply, data = self.execute(mine, packet)
            reply[9] = len(data)
            client.send(REPLY.pack(*reply) + data)

    def execute(self, mine, packet):
        reply = [MAGIC, OK, 0, 0, 0, 0, 0, 0, 0, 0]
        if len(packet) < REQUEST.size:
            reply[1] = PROTOCOL
            return reply, b""
        magic, version, operation, handle, value, value_hi, capacity, \
            length = REQUEST.unpack_from(packet)
        self.requests[operation] += 1
        data = packet[REQUEST.size:]
        if (magic != MAGIC or version != VERSION or length != len(data)):
            reply[1] = PROTOCOL
            return reply, b""
        if operation in (CONVERT_OPEN, CONVERT) or \
                (operation == CLOSE and handle in self.converters):
            return self.conversion(operation, handle, value,
                                   capacity if operation == CONVERT
                                   else value_hi, data, reply)
        if FONT_QUERY <= operation <= MIDI_SYSTEM_FONT or \
                (operation in (CLOSE, GAIN, PAUSE) and
                 (handle in self.midis or handle in self.uploads)):
            return self.synth_request(operation, handle, value, value_hi,
                                      data, reply)
        if value_hi:
            reply[1] = INVALID
            return reply, b""
        return self.voice_request(mine, operation, handle, value, data,
                                  reply), b""

    def conversion(self, operation, handle, value, value_hi, data, reply):
        if operation == CONVERT_OPEN:
            converter = self.convert.astra_audio_converter_open(value,
                                                                value_hi)
            if handle != 0 or data or not converter:
                reply[1] = INVALID
                return reply, b""
            self.next_handle += 1
            self.converters[self.next_handle] = converter
            self.conversions += 1
            reply[2] = self.next_handle
            return reply, b""
        converter = self.converters.get(handle)
        if converter is None:
            reply[1] = BAD_HANDLE
            return reply, b""
        if operation == CLOSE:
            self.convert.astra_audio_converter_close(
                self.converters.pop(handle))
            return reply, b""
        if value > CONVERT_END or not 0 < value_hi <= \
                PACKET_FRAMES * MAX_FRAME_BYTES:
            reply[1] = INVALID
            return reply, b""
        if data:
            reply[1] = self.convert.astra_audio_converter_write(
                converter, data, len(data))
            if reply[1] != OK:
                return reply, b""
        if value & CONVERT_END:
            self.convert.astra_audio_converter_end(converter)
        out = ctypes.create_string_buffer(value_hi)
        moved = self.convert.astra_audio_converter_read(converter, out,
                                                        value_hi)
        reply[4] = self.convert.astra_audio_converter_ready(converter)
        return reply, out.raw[:moved]

    def sound_font(self, synth, name):
        """A shared font, judged by the daemon's own synthesizer code."""
        if name is None:
            return NOT_FOUND
        encoded = name.encode("latin-1")
        if not self.synthesis.astra_audio_synth_sound_font_name(encoded):
            return INVALID
        fd = self.synthesis.astra_audio_synth_open_sound_font(encoded)
        if fd < 0:
            return NOT_FOUND
        os.close(fd)
        self.synthesis.astra_audio_synth_add_font(synth, b"sound:" + encoded)
        return OK

    def font_path(self, digest):
        return os.path.join(self.font_directory, digest.hex() + ".sf2")

    def synth_request(self, operation, handle, value, value_hi, data, reply):
        """Fonts and MIDI voices, judged as the daemon judges them."""
        out = b""
        midi = self.midis.get(handle)
        if operation == FONT_LIST:
            directory = os.path.join("/proc/self/fd", str(self.sound_fonts))
            names = sorted(name for name in os.listdir(directory)
                           if self.synthesis.astra_audio_synth_sound_font_name(
                               name.encode("latin-1")) and
                           os.path.isfile(os.path.join(directory, name)) and
                           not os.path.islink(os.path.join(directory, name)))
            try:
                with open(os.path.join(directory, "default")) as handle:
                    defaults = {line.strip() for line in handle}
            except OSError:
                defaults = set()
            for name in names[value:value + 8192 // 140]:
                size = os.path.getsize(os.path.join(directory, name))
                out += struct.pack(">III", 1 if name in defaults else 0,
                                   size >> 32, size & 0xFFFFFFFF) + \
                    name.encode("latin-1").ljust(128, b"\0")
            reply[3] = len(names)
        elif operation == FONT_QUERY:
            if not os.path.exists(self.font_path(data)):
                reply[1] = NOT_FOUND
        elif operation == FONT_BEGIN:
            self.next_handle += 1
            self.uploads[self.next_handle] = [value, data, bytearray()]
            reply[2] = self.next_handle
        elif operation == FONT_DATA:
            upload = self.uploads.get(handle)
            if upload is None or value != len(upload[2]):
                reply[1] = INVALID
            else:
                upload[2] += data
        elif operation == FONT_END:
            size, expected, body = self.uploads.pop(handle)
            digest = hashlib.sha256(body).digest()
            if len(body) != size or (expected and expected != digest):
                reply[1] = INVALID
            else:
                with open(self.font_path(digest), "wb") as handle_:
                    handle_.write(body)
                out = digest
        elif operation == MIDI_OPEN:
            if data:
                reply[1] = INVALID
                return reply, b""
            synth = self.synthesis.astra_audio_synth_open(RATE)
            try:
                with open(os.path.join("/proc/self/fd", str(self.sound_fonts),
                                       "default")) as handle:
                    names = [line.strip() for line in handle
                             if line.strip() and not line.startswith("#")]
            except OSError:
                names = []
            for name in names or [None]:
                status = self.sound_font(synth, name)
                if status != OK:
                    self.synthesis.astra_audio_synth_close(synth)
                    reply[1] = status
                    return reply, b""
            self.next_handle += 1
            self.midis[self.next_handle] = Midi(self.next_handle, synth)
            reply[2] = self.next_handle
        elif midi is None and operation != CLOSE:
            reply[1] = BAD_HANDLE
        elif operation == MIDI_SYSTEM_FONT:
            reply[1] = self.sound_font(midi.synth, data.decode("latin-1"))
        elif operation == MIDI_FONT:
            path = self.font_path(data)
            if not os.path.exists(path):
                reply[1] = NOT_FOUND
            else:
                self.synthesis.astra_audio_synth_add_font(midi.synth,
                                                          path.encode())
        elif operation == MIDI_LOAD:
            if value == 0:
                midi.song, midi.size = bytearray(), value_hi
            if value != len(midi.song) or value_hi != midi.size:
                reply[1] = INVALID
            else:
                midi.song += data
                if len(midi.song) == midi.size:
                    self.synthesis.astra_audio_synth_load(
                        midi.synth, bytes(midi.song), midi.size)
        elif operation == MIDI_PLAY:
            midi.plays += 1
            reply[1] = self.synthesis.astra_audio_synth_play(
                midi.synth, -1 if value == MIDI_FOREVER else value)
        elif operation == MIDI_STOP:
            self.synthesis.astra_audio_synth_stop(midi.synth)
        elif operation == MIDI_STATUS:
            report = (ctypes.c_uint32 * 6)()
            self.synthesis.astra_audio_synth_report(
                midi.synth, ctypes.cast(report, ctypes.c_void_p))
            reply[1] = self.synthesis.astra_audio_synth_status(midi.synth)
            reply[3] = report[0]
            out = struct.pack(">6I", *report)
        elif operation == MIDI_PRESETS:
            capacity = 8192 // 28
            presets = (ctypes.c_uint8 * (28 * capacity))()
            copied, total = ctypes.c_uint32(), ctypes.c_uint32()
            reply[1] = self.synthesis.astra_audio_synth_presets(
                midi.synth, value, ctypes.cast(presets, ctypes.c_void_p),
                capacity, ctypes.byref(copied), ctypes.byref(total))
            if reply[1] == OK:
                raw = bytes(presets)[:28 * copied.value]
                out = b"".join(struct.pack(">H", struct.unpack_from(
                    "<H", raw, at)[0]) + raw[at + 2:at + 28]
                    for at in range(0, len(raw), 28))
                reply[3] = total.value
        elif operation == MIDI_EVENTS:
            reply[1] = self.synthesis.astra_audio_synth_events(
                midi.synth, bytes(data), len(data) // 4)
        elif operation == MIDI_SET:
            reply[1] = self.synthesis.astra_audio_synth_set(midi.synth, value,
                                                            value_hi)
        elif operation == PAUSE:
            self.synthesis.astra_audio_synth_pause(midi.synth, value)
        elif operation == GAIN:
            midi.gain = value
        elif operation == CLOSE:
            if handle in self.uploads:
                del self.uploads[handle]
            else:
                self.synthesis.astra_audio_synth_close(
                    self.midis.pop(handle).synth)
        return reply, out

    def voice_request(self, mine, operation, handle, value, data, reply):
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
                reply[4] = int(voice.queued + 0.999)
        elif operation == STATUS:
            reply[4] = int(voice.queued + 0.999)
        elif operation == FINISH:
            voice.finished = True
        elif operation == PAUSE:
            voice.paused = bool(value)
        elif operation == CLEAR:
            voice.queued = 0.0
        elif operation == CLOSE:
            del mine[handle]
            voice.queued = 0.0
            voice.closed = True
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
                        help="SDL's test/sample.wav; the gate installs it "
                        "as /home/sample.wav")
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
        astra_image._replace_volume_file(image, arguments.wav,
                                         "/home/sample.wav")
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
            if not terminal_gate.open_terminal(machine, 120, 60):
                raise RuntimeError("no terminal")
            # loopwave is a command: it plays until Ctrl-C, which SDL turns
            # into SDL_QUIT, so it closes its stream and exits 0 -- the
            # marker only prints if the shell saw a normal exit.
            before = machine.sequence()
            machine.qmp.type_line("loopwave /home/sample.wav; "
                                  "print ASTRA-LOOPWAVE-EXIT-$?")
            deadline = time.monotonic() + loop_seconds * 3 + 90
            need = (loop_seconds + arguments.seconds) * rate
            while True:
                with host.lock:
                    host.drain()
                    voices = [v for v in host.voices
                              if v.format == LOOPWAVE]
                    heard = sounding(voices[0]) if voices else 0
                    # Underruns while playing; the queue that drains after
                    # Ctrl-C, before the stream closes, is the stream's end.
                    gaps = voices[0].gaps if voices else 0
                if heard >= need:
                    break
                if arguments.profile and heard and not profiling:
                    subprocess.run(profile + ["start", "audio"], check=True)
                    profiling, profiled = True, (time.monotonic(), heard)
                said = machine.said(before)[0]
                # The typed line echoes the marker; only a line that is the
                # marker says the shell got control back.
                if (any("ERROR:" in line for line in said) or
                        any(line.startswith("ASTRA-LOOPWAVE-EXIT-")
                            for line in said)):
                    raise RuntimeError("loopwave stopped: %r" %
                                       machine.said(before)[0][-20:])
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        "loopwave gave %d of %d frames (voice formats %r): "
                        "%r" % (heard, need,
                                [hex(v.format) for v in host.voices],
                                machine.said(before)[0][-20:]))
                time.sleep(0.5)
            if profiling:
                subprocess.run(profile + ["stop"], check=True)
                print("profiled %.1f s, %d frames heard" %
                      (time.monotonic() - profiled[0], heard - profiled[1]))
            machine.qmp.chord("ctrl", "c")
            if machine.wait_for_text("ASTRA-LOOPWAVE-EXIT-0", 30, before,
                                     exact=True)[0] is None:
                raise RuntimeError("Ctrl-C did not end loopwave clean: %r" %
                                   machine.said(before)[0][-20:])
            said = "\n".join(machine.said(before)[0])
            if "Using audio driver: astra" not in said:
                raise RuntimeError("loopwave did not use the Astra driver")
            with host.lock:
                voice = [v for v in host.voices if v.format == LOOPWAVE][0]
                if not voice.closed:
                    raise RuntimeError("loopwave exited without closing "
                                       "its stream")
                if arguments.save:
                    with open(arguments.save, "wb") as handle:
                        handle.write(voice.written)
                frames = verify_stream(voice, sample.astype(numpy.int16),
                                       arguments.seconds)
            names = {OPEN: "open", WRITE: "write", STATUS: "status",
                     GAIN: "gain", CLOSE: "close", FINISH: "finish",
                     PAUSE: "pause", CLEAR: "clear"}
            print("host requests: " + ", ".join(
                "%s %d" % (names.get(op, op), count)
                for op, count in sorted(host.requests.items())))
            print("SDL upstream loopwave QEMU: PASS (command; %d frames of "
                  "S16BE mono 22050 Hz, bit-exact sample.wav looped; "
                  "Ctrl-C closed the stream and exited 0; %d queue "
                  "underruns)" % (frames, gaps))
        finally:
            machine.close()
            host.close()


if __name__ == "__main__":
    main()
