# Generic file codec architecture

Status: native Codec Kit deployed on DE25, 2026-10-27. ABI 1 is not yet frozen
for third-party providers.

The current `codec.library.1` and `CODEC` service use the existing bounded
HOST0 channel and an isolated AArch64 worker. WAV PCM, Ogg Vorbis, and
YUV4MPEG2 4:2:0 use the same content probe, open, read, seek, and close
lifecycle. The native guest test checks decoded WAV bytes and video frame
bytes, stride, timestamp, seek, and cleanup. SDL_mixer's Ogg adapter consumes
the same library; MP3 and FLAC still use SDL_mixer's bundled decoders.
QEMU tests and guarded DE25 native, SDL_mixer, and asset-backed Quake trials
passed without a panic. Exact final staged release
`0170a92de17493885576ca6a79e34703f49cf47b7c2e506fc958c0a69f1a8be2`
passed native and Quake private-image trials. Its two measured Quake windows
had 20 and 18 software audio gaps in 15 seconds, respectively, with zero
hardware underruns. This did not qualify sustained smooth music. On
2026-10-27, SDL_mixer moved Ogg decode ahead of its existing callback.
The revised adapter passed QEMU and a guarded 180-second DE25 Quake trial:
two 15-second gameplay windows had zero software gaps and zero HDMI
underruns at 21.18 and 20.84 presents/s. A 20-second final-mix capture
had no silence lasting 5 ms at -90 dB. The full trial counted three software
gaps; two were logged at startup/shutdown boundaries. Guest telemetry then
found 59 music callback underflows masked by effects in the final mix. A
larger queue reduced these to one startup burst of two underflows; the final
candidate waits for preroll after SDL_mixer's mandatory post-play seek. Its
exact release `036227968411287ff384cce78b66c0a24f5d520698dbdf7ca25ad814709885aa`
passed QEMU SDL_mixer and native WAV/video gates. Guarded DE25 trials passed
native certifier and 180 seconds of Quake: zero guest music underflows at track
close, zero HDMI underruns/gaps over 20 seconds, and zero host source gaps in
two measured gameplay windows. User heard continuous music and effects.
The same software tree was rebuilt from merged `main` and promoted as
production release `b46aaf2de9af55540162e42ac694cd04e96b48527f4d471201046c509bf98270`.
A preceding
separate-PCM-voice design was rejected after 1,708 source gaps over its trial.

Current provider selection is a compiled-in table inside the isolated worker.
Installable provider registration, independent provider crash isolation, and
conflict policy described below remain design work. Current video output is
timestamped YUV420P chunks; there is no Display-owned YUV surface handoff or
video playback client yet. The guest certifier validates frame lifetime only
through completion of one stream read and close, not Display surface lifetime.

## Existing mechanisms to reuse

- `host.h` and `host_channel_client.c` already supply capability-checked, bounded
  host channels. Fonts uses them to send source bytes to an isolated HPS
  worker. Codec Kit should use this transport, with its own service and
  stream lifetime, rather than add another guest-to-host socket protocol.
- `audio_stream.h`, `pcm.library`, and the HPS audio mixer already provide
  buffered playback, multiple voices, format conversion, gain, and host
  synthesis. SDL_mixer's native MIDI adapter proves that a game can control
  host-rendered music while effects use its ordinary SDL audio stream. Codec
  Kit offloads Vorbis decode; SDL_mixer consumes decoded PCM through its
  existing stream so music and effects share one clock. A separate host PCM
  voice caused 1,708 source gaps in a guarded trial and is not retained.
- The Display service owns surfaces, uploads, presentation, and access to
  Astraea. Video decode must hand timestamped frames to a Display-owned
  surface path. A codec provider must not write arbitrary graphics RAM or
  bypass Display's surface lifetime and presentation rules.
- Haiku's `src/kits/media/MediaDecoder.cpp` selects a decoder, negotiates its
  output and consumes chunks through one client interface. Its
  `src/add-ons/media/plugins/ffmpeg/AVCodecDecoder.cpp` uses that same
  interface for encoded audio and video. Astra adopts those boundaries, with
  native service capabilities and host channels in place of Haiku add-ons.
- Haiku's `src/add-ons/media/media-add-ons/mixer/MixerCore.cpp` runs a
  scheduled mixer thread; `src/kits/media/SoundPlayer.cpp` fills missing
  buffer bytes with silence. Astra adapts those patterns with a separate
  decoder thread, bounded SDL audio stream, post-seek preroll, and explicit
  underflow counters. Codec reads never block SDL_mixer's audio callback.

Codec selection, stream lifecycle, seek/flush, progress, timestamps, and
failure reporting are shared. PCM voices and video surfaces remain distinct
typed sinks: forcing both through one raw-frame buffer would add copies and
discard their existing clock and ownership contracts. The caller supplies
compressed bytes from Astra's VFS; the HPS provider receives no host path or
filesystem authority. Real-time streams use bounded, backpressured chunks;
the font worker's whole-source upload is not a media-stream model.

## One mechanism for file formats

Astra discovers installable userspace codec providers. A provider advertises
stable format identifiers, decode and/or encode capability, and the type of
output it produces or accepts. The registry and lifecycle are generic: audio,
images, video, archives, documents, and future encoded data use the same
discovery and stream contract. Typed libraries interpret the result: Audio Kit
receives PCM frames, Graphics Kit receives image surfaces, and a future Video
Kit receives timestamped frames. A codec does not become a kernel driver and
applications do not need one hardcoded branch per format.

There is one native client API, not a public API for each format. A caller asks
the Codec Kit to probe a source, opens the selected format for decode or encode,
transfers chunks, and closes the stream. The Kit owns provider selection and
translates these calls to one versioned provider protocol. A new codec package
implements that protocol and declares its media kind, formats, directions, and
typed output description; applications do not link to it or learn its private
entry points. SDL_image and SDL_mixer remain compatibility APIs, but their
Astra backends use Codec Kit for each format that has a native provider. Once
a format is qualified through Codec Kit, remove its duplicate in-tree SDL
decoder. Native applications never depend on SDL for codec selection or
decoding.

The client-facing shape is deliberately small: `probe(bytes, hint)` identifies
supported content, `open(format, direction, media_kind)` starts a stream,
`transfer(input, output)` reports bytes consumed and produced, and `close()`
releases it. These names describe the contract, not frozen C symbols. The
client library owns file, memory, or network I/O and feeds bytes to the
provider, so codecs do not need a POSIX descriptor or direct access to a
caller's `AstraFile`. A tagged result describes PCM frames, image pixels, or
other typed output; the caller chooses the media kind and output format but
does not choose a codec-specific function. Transport chunk sizes are negotiated
and partial progress is explicit. Never make an entire file fit in one IPC
message or buffer merely to satisfy this API.

The provider protocol should use ordinary Astra process/service IPC rather
than assume `dlopen` exists: the current loader has no runtime `dlopen` or
`dlsym` facility. The first provider must prove discovery and stream cleanup
before this becomes a public ABI. A provider crash fails only its streams;
the registry, desktop, and kernel remain available.

The concrete installation path is the existing service manager: Codec Kit
publishes one `CODEC` capability, and each installable codec is a separate
service whose definition depends on `CODEC`. Providers register a versioned
factory endpoint with the registry when ready. Clients receive the generic
`CODEC` capability and link only `codec.library`; they neither link provider
libraries nor know provider service names. Registry enumeration returns
provider endpoints to the client library, which performs bounded probes with
deadlines. A slow or crashed provider must not block the registry or another
client. The selected provider serves the stream directly, with its own session
and transfer area; provider death invalidates only that session. Registration
and stream counts grow subject to available resources, not a hardcoded table.

Decoded data has a *media-kind* contract, not a *file-format* contract. For
example, all image decoders return a negotiated pixel format, dimensions and
row stride; all audio decoders return negotiated PCM format, rate, channels and
frame count. A caller may know how to display pixels or play PCM, but never
needs to know how PNG or MP3 was encoded. The generic byte-stream operations
and error statuses are identical across kinds. SDL_image and SDL_mixer adapt
their public calls to those typed results; neither becomes a provider
dependency of native applications.

The provider contract has these operations:

1. **Probe** a bounded byte prefix and optional file-format metadata without
   consuming the source. Return a format identifier and whether the bytes
   match, not a guess based on the filename.
2. **Open** a decoder or encoder against a byte source/sink and negotiate a
   typed output/input description (sample format, image dimensions and pixel
   format, timed video tracks, etc.).
3. **Transfer** in bounded chunks with explicit progress, end-of-stream,
   backpressure, seek requirements, and error results. No whole-file load is
   required to use a codec.
4. **Close/cancel** and release every stream resource even after a malformed
   file, provider crash, or peer loss.

Read and write are separate advertised capabilities. A decoder for MP3 does
not imply an MP3 encoder. Providers may use shared libraries or isolated
processes behind this contract; untrusted complex parsers should run in a
protected process so their failure cannot take down the desktop or kernel.
The host may perform expensive decoding where target measurements justify it,
but applications still see one native Astra interface and the same format
selection behavior.

## Format identity

Filesystem metadata may store a format identifier, but it is only a hint.
The registry uses metadata to narrow candidates and validated magic or
container headers to confirm a match. Each selected codec validates its full
stream. A missing extension, renamed file, or misleading extension does not
change the decoded format. A metadata/header conflict, ambiguous match, or
unknown format is reported explicitly; Astra never silently forces bytes
through a claimed codec. Extensions may be shown to users but have no
authority in codec selection.

Probe reads are bounded and side-effect-free. Selection is deterministic when
multiple providers recognize a format, with installed provider policy visible
to the user. Codec packages are versioned and registered/unregistered with
the normal package mechanism; no kernel update is needed to learn a format.

## First implementation gate and remaining acceptance

Build one small generic registry and streaming contract with the first WAV/PCM
provider. Add Vorbis through the same contract and qualify it with SDL_mixer
on DE25; then prove a video provider uses the same registry and lifecycle
without changing them. A video frame's format, stride, timestamp, and surface
lifetime must survive that test. Test correct metadata and magic, missing and
misleading extensions, metadata/header conflicts, truncated and hostile
headers, unknown formats, partial streams, provider failure, and cleanup.
Keep format-specific rules inside providers; do not freeze a universal codec
ABI from this document alone.
