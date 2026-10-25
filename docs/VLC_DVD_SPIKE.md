# DVD playback and VLC port probe (2026-10-22)

Status: physical DVD decryption, navigation, and 30-second software decode pass
on DE25. Native HDMI playback failed its first timing trials; certification
remains open.

The DE25 sees the Pioneer BDR-XD08 as `/dev/sr0`. Astra auto-mounted the
SpongeBob disc as read-only UDF `DVD_VIDEO` at `/cd0`; native directory
enumeration finds `VIDEO_TS`, IFO/BUP files and VOB segments. This proves
filesystem access, not successful video decode, DVD navigation, or playback.
Linux `DVD_READ_STRUCT` reports copyright protection system type
`cpst=1` on layer 0. A minimal MPEG-2 decoder could not identify video
from raw `VTS_01_1.VOB` even after a 20 MiB probe. DVD-aware decryption and
navigation now work on the physical disc, as measured below.

## Port probe

On Beast, VLC 3.0.24 source from
<https://download.videolan.org/pub/videolan/vlc/3.0.24/> was verified with
SHA-256 `e7cab503d1d7d5849b89d2cf0e1ee60d0ef6d012407791b644b9cfc0cc225fdf`.
Cross configuration for `m68k-unknown-elf` passed with Astra headers,
`m68k-astra-gcc`, and optional codecs/UI disabled. VLC 3 configuration
refuses a non-shared plugin build unless its normal VLC front end is also
disabled; this probe therefore builds only static core libraries. It is not
a runnable player. Scratch build and log on Beast:
`/var/tmp/vlc-astra-build/`.

The first missing Astra facility was unnamed process-private POSIX
semaphores. `sw/userspace/posix/src/semaphore.c` now maps them to native
counting semaphore handles; host semantics test, target build, and complete
POSIX library contract pass on Beast. VLC's `compat/lfind.c` also omitted
`<stddef.h>` when built against this target; the scratch source was patched
to continue the probe. The next core build stops in
`include/vlc_network.h` at absent `NI_NUMERICHOST` and in
`src/input/vlm.c` at absent pthread cleanup macros. Further unresolved
dependencies include POSIX cancellation semantics, network name-info API,
DVD navigation/read libraries, MPEG-2/audio decoders, plugin registration
without runtime `dlopen`, an Astra video output, and synchronized audio.
Do not substitute empty stubs for these contracts.

## Hardware capacity

Latest qualified full-route DE25 shell
`9a5531c3f98c0cb384fa35c5007574f85c4c9e15e3c72d0572f0d494ed8cadb3`
uses 40,754/46,800 ALMs, 317/358 RAM blocks, and 67/376 DSPs.
It leaves 6,046 ALMs, 41 RAM blocks, and 309 DSPs. Every production clock
passes, but worst setup slack is only +0.007 ns; graphics slack is +0.259 ns.
These counts permit a small color-conversion experiment, not a claim that
MPEG-2 decoding fits. Full-route timing with all production features and
physical DE25 verification are required for any RTL addition. The current
HPS exposes no `/dev/video*` or `/dev/media*` decoder device.

Existing SDL texture uploads use Astraea for supported RGB blits and scaling,
but unsupported YUV uploads are CPU-converted. Avoid a DVD- or VLC-specific
FPGA path. The first decode candidate is an isolated native Codec Kit
provider using HPS software decoding behind the versioned Astra protocol in
`docs/CODEC_ARCHITECTURE.md`; applications keep one native media API.

A bounded DE25 test used a static AArch64 FFmpeg 9.0.2 diagnostic, generated
`testsrc2` MPEG-2 program stream at 720×480, 29.97 fps, 6 Mbit/s, and
the production runtime. Decode-only consumed 300 frames in 0.696 s wall
(`utime=0.655s`, 9,004 KiB peak RSS, 14.4× clip-time/wall-time).
Decode plus YUV420-to-RGB565 conversion consumed 1.131 s wall
(`utime=1.123s`, 11,564 KiB peak RSS, 8.85× clip-time/wall-time).
Commands were bounded by `timeout 30s` and ran with `nice -n 10`.
The benchmark did not measure DVD access/decryption, audio, interprocess
transfer, display presentation or synchronization. Production services stayed
active with zero restarts. Temporary board binary, clip and read-only DVD mount
were removed. A generic hardware converter is not justified by this DVD
resolution baseline; remeasure inside the actual playback pipeline.

## Physical encrypted DVD: 30-second decode gate

On Beast, cross-built static AArch64 libdvdcss 1.6.0, libdvdread 7.1.1,
libdvdnav 7.0.0, and FFmpeg 9.0.2 from publisher archives. Every archive
matched its publisher SHA-256 file. Scratch sources, builds, and logs are in
`/var/tmp/astra-dvd-playback/` on Beast. FFmpeg's `dvdvideo` demuxer uses
libdvdnav/libdvdread for title and chapter navigation; libdvdcss authenticated
the drive and decrypted CSS-protected title sectors. `DVDCSS_CACHE=off`
prevented persistent key caching. No decrypted movie copy was written.
Raw decrypted-sector piping identified MPEG-2 and AC-3, but produced audio
timestamp warnings; the DVD-aware demuxer avoided them.

Verified archive SHA-256 values: libdvdcss
`7ea556c846b7bfc32d47b41cae56d1863a6b6d5f706bb162778d6f298490977c`,
libdvdread
`a0d47876548bec806774bbf8dbf20bb19ba139464383156b32eb8e59915b90a9`,
libdvdnav
`a2a18f5ad36d133c74bf9106b6445806fa253b09141a46392550394b647b221e`,
FFmpeg `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
Source indexes: <https://download.videolan.org/pub/libdvdcss/last/>,
<https://download.videolan.org/pub/videolan/libdvdread/last/>,
<https://download.videolan.org/pub/videolan/libdvdnav/last/>, and
<https://ffmpeg.org/download.html>.

On physical DE25, title 1 of the inserted `DVD_VIDEO` disc decoded 900 video
frames (30.03 seconds at 29.97 fps, 720×480) and its English AC-3 audio track.
FFmpeg converted video to RGB565 and audio to signed 16-bit stereo PCM at
48 kHz. Decode completed in 6.958 seconds wall, 4.903 seconds user CPU,
10,544 KiB peak RSS, or 4.32 times clip duration. Log:
`/var/tmp/astra-dvd-playback/dvdvideo30.log` on Beast. A second run paced DVD
input at real-time rate, after a three-second initial burst: 900 frames in
29.643 seconds wall, 4.970 seconds user CPU, 8,868 KiB peak RSS. Each full
second after startup delivered 29–34 frames; no later decode stall or decode
error appeared. FFmpeg reported an initial 3.864-second read lag while opening
the disc. Log: `/var/tmp/astra-dvd-playback/dvdvideo30-paced.log` on Beast.
All three production host services remained active with zero restarts.

Both runs sent converted video and PCM to FFmpeg's null muxer. These runs prove
decryption, navigation and decoder throughput on DE25; they do **not** prove
visible or audible, synchronized, stutter-free playback. HPS has no Linux
framebuffer/DRM or ALSA PCM playback device; Astraea and Astra's audio host
own HDMI output. A native Codec Kit/provider data path must deliver timestamped
decoded frames and PCM to an Astra player, then use existing graphics and audio
APIs. Gate completion requires 30 seconds on HDMI, measured presented-frame
intervals and audio underruns, plus removal/error handling. Startup lag needs
buffering or an explicit loading state. DVD software decode and RGB565
conversion have ample measured CPU headroom; FPGA decode/conversion is not
justified by this test.

## HDMI presentation trials and recovery gate (2026-10-23)

A native SDL diagnostic played the same 30-second title segment as 900
320×240 RGB565 frames and 48-kHz stereo PCM through Astra's graphics and
audio APIs. It is a predecoded output-path probe, **not** an integrated DVD
player. Physical RFB capture showed the cartoon in a native window on the
DE25 display. With files on board SD, the first run had 19 presented-frame
gaps above 50 ms and a 3.875-second worst gap. A 32 MiB video read ring cut
video waits to 3.4 ms, but an audio-file read still blocked for 3.2 seconds.
Host audio underrun counters stayed zero, yet the monitor PCM contained a
3.2-second silent interval. Those runs fail video and audio continuity.
In one 20-second 32 MiB-ring window, `astra-top` measured 17.0% guest CPU
for the player, 14.8% for display service, 43.6% host CPU for QEMU's vCPU
thread, 7.6% combined host audio threads, and 34,580 KiB player resident
memory. FPGA scene generation averaged 30.1 presents/s. These averages
hide the observed gaps; per-process and block I/O were not yet sampled.

A later SD-backed trial panicked around frame 270. The preserved report is
`/var/tmp/astra-dvd-playback/panic-20261023.log` on Beast. Exact release-ROM
disassembly locates the fault in `deadline_remove`: a replacement thread slot
was null after the supervisor timer interrupted the worker while it modified
deadline state. The worker can run without a current user thread, so the old
timer guard did not defer this interrupt. `kernel_worker_servicing()` now makes
that case defer until the worker blocks. Process and worker tests, full kernel
build/test/verify, boot test, and a 20-second native QEMU panic smoke on a
matching installed image pass. Candidate ROM SHA-256 is
`c49aeef1b6412e1e465a324748c444fd24df64c81f9b2fca0e87bbffe1857fe6`.
Haiku's `src/system/kernel/scheduler/RunQueue.h` and `scheduler.cpp` were
reviewed read-only: scheduler transitions there require disabled interrupts.
Astra adapts that rule by deferring supervisor-timer deadline mutation during
its worker service, while preserving idle deadline wakeups.

The first private DE25 candidate could not open its audio mailbox because it
was created `root:root 0660`; a scratch-only trial precreates it `root:astra
0660`. Production image hash and release selector matched before and after
restoration. The next private run reached POST but bundled userspace failed
the new volume catalog contract. Current userspace, storage provider and QEMU
were then built and paired with the candidate ROM and a disposable image;
native QEMU reached stage 8, and the DE25 launched services. During this
physical run board SSH stopped sending its banner, while ICMP stayed alive.
No playback measurement or restoration check is available from that run yet.
The 138 MiB raw-video copy had been staged in tmpfs; an earlier DE25 baseline
recorded only 133,668 KiB MemAvailable with no swap. Memory pressure is a
strong hypothesis, not a confirmed cause until board logs are recovered.

Next trial stages video on disk, preloads its 138 MiB into guest RAM before
starting audio/video, and retains the production 512 MiB QEMU allocation.
This avoids extra host tmpfs pressure and makes disk stalls part of startup,
not playback. Gate still requires full 30-second presentation with zero
frame gaps over 50 ms, zero late frames or low-audio samples, zero audio
underruns/gaps, HDMI capture, and measured guest/host CPU, memory, and I/O.
The predecoded probe cannot by itself certify live DVD read/decode/sync.

## Next acceptance gates

1. Add only POSIX facilities VLC actually needs, with native-backed semantics
   and shared tests; finish cross-linking a VLC core and a minimal playable
   configuration. Resolve plugin loading explicitly.
2. Move the qualified DVD read/navigation, CSS decryption, and MPEG-2/AC-3
   decode path behind a native provider contract. Report unsupported disc
   protection clearly; preserve read-only media and avoid persistent keys.
3. Build the native timed-frame/PCM delivery and player presentation path.
   Re-run a bounded title segment on physical HDMI with measured frame timing,
   audio sync and underruns under the production workload.
4. Only if conversion becomes a material CPU cost in the real pipeline,
   prototype generic Astraea YUV conversion. Require full route, all-clock
   timing closure, repeated POST,
   storage, graphics and HDMI gates before replacing the qualified shell.
5. Deliver a native user-facing player opening `/cd0`, with seek, stop,
   removal/error handling, and synchronized sound and picture.

Haiku references reviewed before the POSIX/codec change:
`src/system/libroot/posix/semaphore.cpp` for native-backed POSIX semantics;
`src/kits/media/MediaFile.cpp` and `MediaDecoder.cpp` for source/decoder
separation and format negotiation; `src/servers/media_addon/MediaFilePlayer.cpp`
for player lifetime. Astra's existing Codec Kit design remains the native
contract, with POSIX only adapting VLC.
