# Media data plane: audio and video, the way Haiku does it

Status: approved by the owner 2026-10-05. Phase 0 next; see `docs/HANDOVER_2026-10-05_MEDIA.md`.

Owner direction: copy what Haiku has proven instead of inventing layers; SDL
stays the game API; no direct framebuffer access; games need every cycle.

Haiku references are paths in `~/Git/haiku` (and `~/Git/SDL2` on beast for
SDL's Haiku backend). Astra references are paths in this repository. Board
numbers come from `astra-top` and `de25-ab.sh` (docs/DEBUGGING.md section 7).

## Why: what the board shows

Chocolate Doom on the DE25, release `c2613810`:

- The guest is **24% idle** while Doom draws 19 frames a second: Doom is not
  short of CPU, it waits.
- 950 context switches a second, **810 of them between address spaces**.
  Each flushes QEMU's TLB; on the vCPU host thread ~16% goes to
  translation-block lookup and ~9% to TLB flush and refill.
- The media service wakes ~265 times a second (~6 per 23 ms audio buffer).
  Raising it to priority 24 removed audio gaps under mouse motion (301 -> 0)
  and cost Doom ~2 frames a second, by adding ~200 preemptive cross-space
  switches a second. That trade exists only because data flows through a
  server.

## The four rules Haiku follows

1. **Data never passes through a server per buffer or frame.** Servers set
   up connections, formats and policy. Haiku's `media_server` does nothing
   per buffer; `app_server` draws but SDL's game thread never waits on it.
2. **Shared memory carries the data; a one-way note says it is ready.**
   `BBufferProducer::SendBuffer` sends a buffer id, no reply
   (`src/kits/media/BufferProducer.cpp:437-451`). Buffers come back by
   recycling a semaphore in a shared list, no message
   (`src/kits/media/SharedBufferList.cpp:353-365`). Blocking on a free
   buffer is the backpressure (`SharedBufferList.cpp:282`).
3. **The hardware clock drives everyone.** The output thread blocks until
   the sound hardware's interrupt (`MultiAudioNode.cpp:1765-1889`) and
   publishes played frames and time into a shared time-source area
   (`TimeSource.cpp:383-388`); every node schedules against it. Displays
   pace with a retrace semaphore the driver releases at vblank
   (`PrivateScreen.cpp:268-286`). Nobody polls or guesses a sleep.
4. **Priority goes on the thread with the deadline.** The app's own audio
   thread runs at `B_URGENT_PRIORITY` inside the app
   (`SoundPlayNode.cpp:119`); mixer and output at `B_REAL_TIME_PRIORITY`.
   The game's main thread is normal.

And for games specifically, SDL's Haiku video backend
(`~/Git/SDL2/src/video/haiku`): the framebuffer is a `BBitmap` the game
writes directly (`SDL_bframebuffer.cc:44-78`); present is
`PostMessage` and returns at once (`SDL_bframebuffer.cc:95-103`); the window
thread drops stale pending presents and draws only the newest
(`SDL_BWin.h:452-462`). **The game thread never waits on display latency.**

## Audio

### Today

One SDL buffer (1024 frames, ~23 ms) costs about 3 synchronous round trips
(one WRITE, STATUS polls), ~12 guest switches, 3 interrupts, 15 host thread
handoffs and 6 copies of every sample, 2 of them on the MC68040:

```
SDL thread -> memcpy into area -> port call -> media (one thread)
  -> memcpy into host DMA span -> host channel, media BLOCKS
  -> QEMU worker -> unix socket -> audio daemon (one mutex) -> reply
  -> QEMU -> IRQ -> media -> reply -> SDL; then STATUS polls + guessed sleep
```

`sw/userspace/audio/src/pcm_library.c:271-369`,
`sw/userspace/services/media/main.c:477-626`,
`emu/qemu/qemu-9.2/hw/m68k/astra68.c:3448-3642`,
`fpga/arty/linux/astra_audio_host.c:407-503,1277-1308`. The daemon learns
the hardware clock by polling the 512-frame FIFO every 2 ms; no clock
reaches the guest.

### Target

```
SDL audio thread (priority 24, inside the app)
  waits for a free buffer (kernel wait on the stream's return ring)
  SDL mixes straight into that shared buffer          [no copy]
  pushes its index on the stream's play ring           [one notify]
host mixer, clocked by the FIFO
  reads guest buffers from the play ring, copies into its per-input ring
  (Haiku's mixer does the same, MixerInput.cpp:263-298), returns the index
  on the return ring                                    [one wake per period]
media service: open/close, capability, policy, health -- not per buffer
```

- **Stream memory** is a group of N buffers (Haiku: `BBufferGroup`, count
  `max(3, latency/period + 2)`, period ~10 ms) in memory both the app and
  the host side can address, plus two single-producer rings: *play*
  (app -> host) and *return* (host -> app).
- **SDL** (`SDL_astraaudio.c`): `GetDeviceBuf` returns the next free shared
  buffer, `PlayDevice` pushes it, `WaitDevice` blocks on the return ring.
  `astra_pcm_wait`'s STATUS polling and sleep arithmetic are deleted.
- **Priority**: the SDL audio thread already asks for time-critical priority
  (`SDL_audio.c:694`); that maps to the media band (24) per thread. The
  media service returns to 16.
- **Host**: the daemon's feed thread is the time source. It publishes
  (frames played, monotonic time) in a seqlock record -- the pattern the
  display already uses for scanout timing (`display_mailbox.h:54-68`) --
  and returns buffers as the FIFO consumes them. Its feed thread gets
  real-time scheduling (today `RestrictRealtime=true`); Haiku runs mixer
  and output at real-time priority.
- **Per buffer**: one ring notify app -> host, one return wake host -> app.
  No server, no reply, no polling.

### The stream object (kernel, implemented)

`sw/include/astra/audio_stream.h`; kernel `audio_stream_*` in
`sw/kernel/process.c`; syscalls 107-108 (ABI `0x00010040`).

- **Memory.** The application's own DMA buffer: a 64-byte
  `AstraAudioStreamHeader`, then `buffer_count` (2-32) buffers of
  `period_frames` (64-4096) frames in any `pcm_format.h` format. Buffers go
  to the host strictly in order, so Haiku's two queues (send by id, recycle
  through the shared list) collapse into two monotonic counters: `queued`
  (application only) and `consumed` (host only). A buffer is free while
  `queued - consumed < buffer_count`.
- **Authority.** `ASTRA_SYSCALL_AUDIO_STREAM_OPEN` takes a HOST0 device
  handle with `ASTRA_RIGHT_AUDIO_STREAM` (bit 8). The media service holds
  HOST0 with every right and hands an application a duplicate carrying that
  right alone: it can open streams on its own memory and cannot open a host
  channel. The stream handle is not transferable.
- **One call per buffer.** `ASTRA_SYSCALL_AUDIO_STREAM_WAIT` kicks the host
  (it reads `queued` from the header) and, if no buffer is free, arms a
  one-shot interrupt and blocks. This is the host channel's kick-and-wait
  (`host_channel_wait`). Phase 0 proposed a user-mapped doorbell page
  instead; the kick is folded into the wait because SDL's audio loop always
  waits right after it plays (`PlayDevice` then `WaitDevice`), so a doorbell
  would add an MMIO store and save no trap, and streams need no aperture
  slots. Haiku's `SendBuffer` is a `write_port` system call too.
- **Interrupt.** IRQ_SRC_HOST, acknowledged before the scan with the host
  channels (`kernel_process_host_channel_irq_service`), one waiting bit per
  stream. Nothing disarms: the host disarms as it fires.
- **Lifetime.** OPEN -> DEAD when the stream handle or the DMA buffer under
  it closes: the host is told to stop (`HOST_ACCEL_STREAM_CONFIG` CLOSE)
  before the DMA pin ends, as host channels are. A dead stream keeps its
  slot until its handle closes, so a slot is never renamed under a handle.
- **Registers.** Vesta `0x8E0` STREAM_CONFIG (physical
  `AstraAudioStreamConfig`), `0x8E4` STREAM_RESULT, `0x8E8` STREAM_KICK
  (slot), `0x8EC` STREAM_ARM (slot). `ASTRA_HOST_CAP_AUDIO_STREAM` (bit 9)
  says the host has them; without it OPEN answers UNSUPPORTED.

### The rest of the path (implemented)

- **QEMU** (`astra68.c`, `astra_audio_stream_*`): STREAM_CONFIG writes the
  header; STREAM_KICK (vCPU thread) reads `queued` and copies each queued
  buffer that fits into the stream's ring in the **audio mailbox**, then
  writes `consumed` -- the buffer is the application's again at once, as
  Haiku's mixer recycles a buffer as soon as it has copied it. Only the
  kick reads `queued`, right after the guest's own stores: the main loop,
  refilling on a daemon wake, works from QEMU's copy, so a weakly ordered
  host (the DE25's A76) never sees the count before the samples.
- **Audio mailbox** (`sw/include/astra/audio_mailbox.h`,
  `ASTRA_AUDIO_MAILBOX_PATH`, `/run/astra/audio.mailbox` on the board): one
  shared file, a 64 KiB byte ring per stream slot holding at most two
  buffers. QEMU writes `written`; the daemon writes `read` and, when its
  reading makes room for another buffer, bumps `room_sequence` and
  futex-wakes it. A QEMU thread sleeps on that word and hands the wake to
  the main loop. One wake per buffer each way, no reply.
- **Daemon** (`astra_audio_host.c`, `pull_streams`): each open slot is a
  voice like any socket client's, mixed by the same `mix_frame`. The feed
  thread decodes from the ring just the source frames the next mix needs
  (the filter's wing plus the FIFO's room at the stream's rate), so the
  ring, not the voice queue, is where a stream waits and its latency stays
  two buffers plus the application's.
- **pcm.library 2.5** (`AstraPcmBuffers`, `pcm_buffers.c`): open asks the
  media service once for the grant (`ASTRA_PCM_STREAM_GRANT`), creates the
  DMA buffer, opens the stream and drops the grant. `get` names the next
  buffer, `queue` stores `queued` (no system call), `wait` is the one
  system call per buffer. The media service is not involved again.
- **SDL** (`SDL_astraaudio.c`): three buffers of `spec.samples` frames;
  `GetDeviceBuf` is the shared buffer (SDL mixes in place), `PlayDevice`
  queues it, `WaitDevice` waits. Without streams (a host with no mailbox)
  SDL falls back to the media service's voice.
- **Priority** (kernel `thread_priority_ceiling`): an open audio stream is
  the authority for the media band. While a process holds one it may place
  its threads up to `ASTRA_PROCESS_PRIORITY_MEDIA` (24); applications
  otherwise stop at 19. SDL's audio thread goes there in the backend's
  `ThreadInit` (SDL's own TIME_CRITICAL request goes through SCHED_OTHER,
  which means nothing on Astra). The media service, off the data path,
  returns to 16.
- **Left out, on purpose:** the clock record (the design's seqlock of
  frames played) -- buffers return when copied, and no client asks where
  the output is; per-stream gain and pause -- no client sets either; a
  SCHED_FIFO feed thread in the daemon -- motion gaps are already 0 on the
  board. Each is for when something needs it.

### Deleted

STATUS polling and guessed sleeps; the media service's WRITE relay and its
per-write host command; the per-write socket round trip; both MC68040
copies.

## Video

### Today

Doom presents through SDL's renderer. One frame is **5 strictly serialized
device round trips** (texture upload, three draw-list flushes, present):

- every client call is a synchronous port call;
- `builder_begin` calls `settle()`, so each batch waits for the previous one
  to finish on the FPGA (`window_graphics.c:210-226`, `main.c:4106-4113`);
- present waits for the previous batch and then for the compositor batch
  (`main.c:2570-2590, 2417-2424`);
- the device takes one request at a time (`platform.c:523-527`, kernel
  `display_dma_active`); QEMU copies every batch and attachment on the vCPU
  thread inside the MMIO store (`astra68.c:917-1019`); the helper polls the
  FPGA for completion (`astra_graphics_hw.c:680-705`).

Plus two CPU copies of the texture (a private malloc lock buffer, then
`AREA_COPY_IN`), ~1,200 display-service syscalls a second and a STATE event
on every present. Full map: `docs/HANDOVER_2026-10-05_MEDIA.md`.

### Target (SDL stays the API)

1. **Present is posted, not called.** The game thread hands the frame over
   and continues, exactly as SDL's Haiku backend posts
   `BWIN_UPDATE_FRAMEBUFFER`. A newer present replaces an unprocessed older
   one (latest wins). `SDL_RenderPresent`'s only wait is the vsync wait,
   which already exists (`SDL_astrarender.c:1104-1107`) -- Haiku's
   `WaitForRetrace`.
2. **One submission per frame.** The draw lists of a frame travel in the
   shared list area the service already maps; target changes mark points in
   it instead of each being a round trip. No reply per flush.
3. **The display service pipelines.** Building frame N+1 never waits for
   the device to finish frame N: two batch buffers and a request queue of
   at least two (kernel and Vesta), with completion per request. The
   `settle()` in `builder_begin` goes. The mailbox keeps one request: the
   DE25 host arena holds exactly one batch and the engine runs one at a
   time, so QEMU starts the queued request when the helper completes the
   last (phase 0, `docs/HANDOVER_2026-10-05_VIDEO.md`).
4. **Textures are written where the device reads them.** Streaming textures
   lock straight into the shared staging area (the existing ponytail note at
   `SDL_astrarender.c:271-272` names this), removing the malloc copy and
   `AREA_COPY_IN`. Doom's own 8-to-32-bit conversion remains.
5. **QEMU does not copy on the vCPU thread.** Attachments are handed to the
   helper by reference (extents), or the copy moves to QEMU's display
   thread; the DE25 host arena already gives the engine a zero-copy batch.
6. **Present flips a content bank** (three exist per window) with a posted
   latest-value word, the cursor's pattern (`DISPLAY_CURSOR`, mailbox 1.8).
   The STATE event is sent only when state changes.
7. **Fences are real** so a client knows when an upload finished and can
   reuse memory (Haiku: buffer recycling). Today they are born signaled.

Per frame: the game makes one posted submission and one vblank wait; the
display service does one build and one device request, overlapped with the
next frame.

## Phases, each measured on the board

Each phase lands with its gate and an `astra-top` / `de25-ab.sh` A/B
against the release before it. The numbers to watch: Doom presents/s, audio
gaps (idle and motion), guest idle %, cross-space switches/s, media and
display CPU and runs/s, vCPU host overhead (`--perf`).

0. **Verify the unknowns (no code).** How a guest ring notify reaches QEMU
   (a user-mode doorbell, as the host channel kicks); which memory both the
   app and QEMU can address (DMA buffers are creator-only today; areas are
   scattered pages QEMU cannot see, except display attachments); how the
   audio daemon reaches guest buffers (socket today; the display mailbox
   shows a shared-memory + futex path). Fix the kernel switch-cause counters
   (`blocked` reads 0) so the A/B can attribute switches.
1. **Audio data plane** as above (data path landed and measured
   2026-10-05: Doom 18.6 -> 21.1 presents/s idle, 13.9 -> 15.6 in motion,
   motion gaps 0, cross-space switches 807 -> 330/s, media 264 -> 8
   runs/s; `docs/HANDOVER_2026-10-05_MEDIA.md`); media service to control only; SDL audio
   thread to 24, media service to 16. Expect: no gaps idle or in motion,
   media runs/s near zero, ~200 fewer cross-space switches/s, Doom back to
   >= 21 presents/s.
2. **Video: posted present and pipelining** (items 1-3). Removes four of
   the five per-frame waits. Expect the 24% idle to turn into frames.
   Landed and measured 2026-10-05: Doom 21 -> 35 presents/s idle (its tic
   cap), 15.3 -> 30.9 in motion, guest idle 34% -> 19% / 8%
   (`docs/HANDOVER_2026-10-05_VIDEO.md`).
3. **Video: copies** (items 4-6).
4. **Fences** (item 7) and retire what the new paths replace.

## Corrections found on the way

- `docs/AUDIO_ARCHITECTURE.md` says a busy WRITE accepts a prefix; the host
  accepts all or nothing per packet (`astra_audio_host.c:1290-1294`).
- The kernel's switch-cause counters do not add up to the switch total
  (`astra-top` shows `blocked 0/s`).
