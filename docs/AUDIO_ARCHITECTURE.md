# DE25 audio architecture

Status: the fixed-format guest-to-HDMI PCM transport is implemented and has
passed a 30-second physical run. A first fixed-format media service and
`pcm.library.2` are being built; their application path has not yet passed a
physical release gate. The optional media service is now in the default
desktop image. The desktop's startup chime is rendered to 48 kHz stereo PCM
on Beast during the build, then streamed from an automatically reaped desktop
thread; the MC68040 does not synthesize it. This document supersedes the
Arty/core-placement assumptions in `OS_VISION.md` for the DE25.

## Hardware and ownership

The production DE25 has one 48 kHz, signed 24-bit stereo HDMI sink. The
existing `astra_hdmi_audio` block accepts one stereo frame at a time through
two 32-bit AXI4-Lite writes and has a 512-frame FIFO, or 10.67 ms at 48 kHz.
Its DE25 register base is `0x20106000` (`0x20100000` control base plus
`0x6000`); the Arty address in `fpga/arty/audio/README.md` is not the DE25
address. The DE25 shell has qualified this output with a test tone, direct
PCM file playback, and two simultaneous fixed-format host PCM clients. No FPGA
synthesis, mixer, DMA engine, or larger FIFO is assumed or required by this
plan.

The Linux host owns the physical sink and performs PCM mixing; resampling,
sample playback, MIDI synthesis, and eventual speech generation will also
reside there. One Linux
audio daemon exclusively maps the audio registers and feeds the FIFO. It
claims a host lock before mapping MMIO, so a competing instance cannot reset
or steal the sink. The future Astra media service will own guest-facing
capability checks, handles, lifetimes,
voice/stream policy, and service recovery. Applications use that service, not
Linux sockets, QEMU internals, or raw MMIO. The kernel only supplies its
existing host-transport, IPC, memory, and scheduling mechanisms.

```text
Astra application -> media service -> batched AstraHost audio commands
                                       -> QEMU host provider -> Linux audio daemon
PCM streams/sample voices/MIDI/speech -> software mixer -> existing FIFO -> HDMI
```

The QEMU boundary is a transport, never an alternate audio API. Reuse the
authenticated, owner-bound AstraHost channel and its bulk data span; do not
cross it for individual samples. The Linux daemon is the only physical-sink
owner and must not block the MC68040 vCPU on synthesis, decoding, or FIFO
refills. A local host IPC hop is acceptable if measurements show that it meets
the audio deadline and keeps audio dependencies out of QEMU.

The current CPU placement gives CPU0 (A55) input/log helpers and SD/Ethernet
IRQs, CPU1 (A55) display rendering, CPU2 (A76) the sole vCPU, and CPU3 (A76)
QEMU main/AIO/filesystem workers. No old Arty core assignment carries over.
The feeder's placement and priority must be chosen from DE25 contention
measurements. It must never run on CPU2. Speech/model loading and asset I/O
must never run in the time-critical feeder.

The 2026-09-24 direct-MMIO baseline on the live DE25 played 30 seconds of
48 kHz stereo signed-24-bit little-endian PCM (1,440,000 source frames plus
64 silence frames) without a new underrun or overflow. The clip was two mixed
sine waves generated on Beast, not guest-submitted audio. On CPU0, wall time
was 30.000 s and user+system CPU time was 1.639 s (5.5% of one A55). A
concurrent 35-second read of `/dev/mmcblk0` pinned to CPU1 still passed:
1.664 s of feeder CPU time (5.5%), zero new FIFO faults. With refill-gap
instrumentation, the idle replay reached a 6.573 ms maximum gap and a 197-frame
minimum FIFO level; the SD-contention replay reached 6.805 ms and 185 frames.
That observed floor is only 3.85 ms of audio, so it is not an ample scheduling
margin. These measurements prove physical sample delivery under this one
contention case; they do not qualify a guest stream, multiple voices, or the
eventual mixer. The existing feeder wakes about 860 times per second, so its
context-switch cost is a candidate for later measurement-driven tuning.

The next checkpoint used an Astra guest certifier, the owner-bound AstraHost
channel, QEMU's audio provider, and a dedicated Linux daemon to send 1,440,000
frames to HDMI in 30 seconds. On the final packaged release the guest reported
zero hardware underruns, overflows, and software gaps; the daemon independently
reported exactly 1,440,000 mixed frames, a 191-frame minimum FIFO level, and
the same zero faults. With the daemon packaged in the production release, two
independent clients each sent 30 seconds of PCM while a direct SD-card read
ran on CPU1.
The mixer emitted 1,440,512 frames (including its silent drain tail), reached
a 178-frame minimum FIFO level on the final release, and reported zero faults.
The no-provider guest boot failed cleanly with `PEER_DEAD`; malformed packets,
cross-client
handles, stale handles, and writes after finish were rejected. A second daemon
was rejected before it could map MMIO or replace the active socket. The
production daemon is ordered before QEMU at boot and stays idle without a
client.

This certifies the fixed 48 kHz stereo signed-24-bit transport and physical
mixer under the tested workload, not the complete PCM release gate below.
Measured end-to-end latency, mono/rate conversion, pan, master gain/mute,
client-death and daemon-restart behavior during playback, and broader
display/input contention still need their own acceptance evidence before the
media service and public Audio Kit are considered stable.

## PCM contract and first release gate

Start with generic, application-supplied PCM. Streams and sample voices have
independent gain, pan, pause, and lifetime; the mixer also has master gain and
mute. Format identifiers must specify sample width, signedness, channels,
rate, and byte order explicitly because the MC68040 is big-endian and Linux
is little-endian. The Linux mixer converts and resamples to one 48 kHz stereo
mix, applies gain with saturation rather than wraparound, and emits 24-bit
signed samples. Each accepted buffer reports completion or a precise failure;
full queues apply backpressure rather than silently dropping audio.

The media service and host daemon must revoke client state on process death,
close, or generation change. Bad lengths, offsets, formats, ownership, and
timestamps fail before publication. Playback restart is explicit: a daemon
restart must not replay stale samples or leave a guest believing an old handle
is live. There is no fixed application or voice count independent of available
memory, handle space, and measured mixer deadline. The 512 hardware frames are
a real FIFO capacity, not an application quota.

The Linux daemon is a systemd service with journal output and automatic
restart. It is a wanted, not required, dependency of the Astra runtime: its
failure must never stop QEMU, the kernel, desktop, or another service. The
guest media service checks provider health, exits on provider loss, and records
the failure for `service inspect media` and `events`. Existing stream handles
do not survive that loss; clients must open new streams after recovery.

The host feeder uses preallocated, locked working buffers; no filesystem I/O,
model loading, unbounded allocation, or ordinary-priority waits occur in its
refill path. A larger Linux software queue absorbs bursty guest delivery but
cannot conceal a feeder stall longer than the hardware's 10.67 ms of queued
frames. Initial period size, queue depth, CPU affinity, and scheduling policy
are tuning variables, not unmeasured constants to freeze in the ABI.

Before this PCM path is accepted, establish an idle and full-system baseline
on the physical DE25. Measure enqueue throughput, worst observed feeder gap,
FIFO level/underrun/overflow counters, end-to-end latency, CPU time by thread,
MC68040 effective rate, display frame health, and host memory. Test silence,
mono/stereo conversion, non-48-kHz input, independent voice volumes, gain
clipping, rapid start/stop, multiple clients, queue pressure, malformed
requests, client death, daemon restart, and storage/display/network contention.
The release gate is uninterrupted playback with no new hardware underruns or
overflows and no unacceptable vCPU, display, or input regression under the
qualified contention workload. If the existing FIFO and host scheduler cannot
meet this gate, stop and measure the failure before changing hardware.

## Port adapters and public libraries

The `pcm.library.2`/NDK slice accepts 48 kHz stereo signed-24-bit LE or
signed-16-bit BE input, with per-stream gain, pause/resume, clear, finish,
status, and close. The Linux mixer converts the 16-bit input to the physical
24-bit sink. A host-side self-test mixes 16 simultaneous voices and rejects
bad formats, partial frames, and invalid controls. A 30-second physical DE25
socket test mixed 16 simultaneous streams, reached a 456-frame minimum FIFO,
and observed zero underruns, overflows, or software gaps. This qualifies the
host PCM mixer under that workload, not the MC68040 application path. The application path
is staged in the default desktop image to exercise the media-service boundary; it is not a stable
general-purpose game-audio contract until the remaining PCM gate passes.
After that gate, add compatibility adapters for the actual upstream port
interfaces, then publish the complete native Audio Kit/NDK API. SDL2's audio
backend can advertise 48 kHz `AUDIO_S16MSB` and forward PCM. SDL2 can convert
other requested formats and rates as a correctness path, but the MC68040 cost
has not been measured. The backend cannot move an application's SDL callback,
SDL2_mixer, or SDL_audiolib code from the MC68040 to Linux. Host-backed
adapters must be built at the appropriate library boundary where measurement
shows guest mixing or decoding is material. Do not patch Doom or DevilutionX
for an Astra-only audio path, and do not claim that a PCM sink alone offloads
their mixing. Native and POSIX applications share the one media service.

The format layer above PCM will use installable userspace codec providers.
WAV/PCM is the first useful file reader and writer; compressed formats such as
MP3 are independent decoder/encoder capabilities, not built into the PCM
stream API. Discovery uses validated file metadata and magic/container bytes,
never the filename extension. A codec must still validate the content after
selection. Decoder output feeds the same native PCM streams and host mixer;
no file parser runs in the kernel or time-critical Linux feeder.

The Audio Kit will eventually expose PCM streams, reusable sample voices,
per-voice gain/pan, a mixer clock/status, and asynchronous completion. Its
public ABI follows working, tested service behavior; it does not precede the
physical PCM gate merely to provide speculative API surface.

## MIDI synthesis, then speech

Only after reliable PCM playback, add host-side synthesis as another source
feeding the same mixer. Each synth voice has independent gain and pan, while
MIDI channel volume and expression remain separate controls. The target is
complete practical song playback: multiple MIDI channels, bank/program
selection, velocity, pitch bend, sustain and other controllers, aftertouch,
tempo changes, polyphony, and configurable SoundFonts with convincing reverb
and chorus. A SoundFont-compatible engine such as FluidSynth is a candidate,
not an ABI dependency or a substitute for DE25 CPU/memory measurements.
Compare representative MIDI songs and overload behavior before choosing its
default SoundFont, effect settings, or polyphony policy. Resource admission
must be based on actual memory and deadline headroom rather than an arbitrary
voice count.

Speech is last. It is an asynchronous host-generated PCM source with its own
gain and cancellation, not work on the feeder or MC68040. Evaluate speech
quality, time-to-first-audio, real-time factor, and resident memory on the
loaded DE25. The public API names voices/capabilities without committing to
one synthesis engine. A lightweight engine may establish functionality; a
neural voice is optional until its memory and timing pass the same PCM
continuity gate.
