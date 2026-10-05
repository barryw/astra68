# Handover 2026-10-05: media data plane (audio and video)

Read `CLAUDE.md` (note the **Haiku first** standing instruction), `AGENTS.md`,
then `docs/MEDIA_DATA_PLANE.md` -- the approved design. This page is the
state, the facts behind the design, and the next steps.

## Owner decisions

- Approved: `docs/MEDIA_DATA_PLANE.md`. Copy Haiku's proven designs; no
  layers invented to be different. SDL stays the game API; no direct
  framebuffer access.
- From now on, check how Haiku does a thing before building it.
- Scheduler priority is one number (1-27; media band 24-27); process
  control is `PROC:<pid>/ctl`; `nice`/`renice` are sbase's. All committed.

## State

- `main` at `eaf92443` plus this page; everything pushed.
- DE25 board is on release `2cf870dc`: a **twin with media at priority 16**
  built from a beast-only edit (not committed). The committed tree says 24.
  Republish from `main` before any new A/B baseline.
- Measurement tools: `astra-top` (guest per-process CPU, idle, switches by
  kind, host threads, presents, audio gaps, `--motion`, `--perf` with host
  symbols) and `de25-ab.sh` in every release's `bin/`; docs/DEBUGGING.md
  section 7. The `sampler` service feeds it via `WORK:.astra/sample`.
- Beast build lock: `~/astra-mg/locked`. Another agent (CDFS, tree
  `~/Git/astra68-cdfs`) shares beast; it now takes the lock. Shared
  `~/ncurses-astra-build` collided once -- builds in other trees must set
  their own `NCURSES_BUILD`/`NCURSES_PREFIX`.
- Verify script on beast now also runs `emu/qemu/test-astra-top.py`
  (backup `~/astra-mg/verify-nopc.sh.bak-20261005`).

## Board baseline (Chocolate Doom, de25-ab.sh, 2 rounds)

| | media 24 (`c2613810`) | media 16 (`2cf870dc`) |
|---|---:|---:|
| presents/s idle | 19.5 / 18.8 | 21.0 / 21.3 |
| presents/s, 125 Hz motion | 14.9 / 14.3 | 15.8 / 16.0 |
| audio gaps in motion / 30 s | 0 / 0 | 344 / 288 |
| guest idle | ~24% | ~23% |
| switches/s (cross-space) | 950 (810) | 790 (610) |
| media runs/s | ~265 | ~243 |

vCPU host thread (`--perf`): 74% translated guest code, ~16% TB lookup,
~9% TLB flush/refill (each cross-address-space switch flushes QEMU's TLB).

## Next: phase 0 (no code), then phase 1

Phase 0 questions, answer each with file:line before writing code:

1. How a guest ring notify reaches QEMU without a server: the host channel
   already kicks a doorbell page from user mode (`host_channel_client.c`
   91-114, QEMU `astra68.c:4964-5038`, doorbell 0xffd00000). Can a stream
   ring get its own doorbell?
2. Memory both an app and QEMU can address: DMA buffers are contiguous and
   QEMU-visible (`astra_dma_data`, `astra68.c:2005-2015`) but creator-only;
   areas are scattered pages QEMU cannot see except display attachments
   (`process.c:2857-2900`). Haiku: `BBufferGroup` = one cloneable area.
3. How the audio daemon reaches guest buffers: today a unix socket per
   command; the display mailbox is the shared-memory + futex precedent
   (`display_mailbox.h`, `astra68.c:6640-6706, 1026-1061`).
4. Where the FIFO clock is published: daemon feed thread (2 ms poll,
   `astra_audio_host.c:479-503`); seqlock pattern from
   `AstraDisplayScanout` (`display_mailbox.h:54-68`).
5. Fix the kernel switch-cause counters so A/Bs attribute switches.

### Phase 0 answers (2026-10-05; file:line in sw/, emu/, fpga/)

1. **Notify.** Doorbell aperture 0xffd00000, 256 x 4 KiB pages, KICK 0x20
   (`host.h:536-556`); user-mode MMIO store, no syscall (`syscall.c:701-713`).
   QEMU already runs many outstanding commands per channel
   (`astra68.c:4964-5038`); only the client uses capacity 1. Channels are
   one per (process, thread) (`process.c:2343-2349`) and not waitable
   (waitables: `process.c:8501-8509`; ring `ring.c:665-704`; IRQ
   `irq.c:1184-1207`; IRQ-safe wake `thread.c:2191`).
   **Decision:** a new kernel object, the *audio stream*: pins the app's DMA
   buffer, takes a doorbell slot (not thread-bound), waitable via
   `prepare_wait` on the return-ring producer word; QEMU raises
   IRQ_SRC_HOST with a per-stream pending bit; IRQ service stays
   ack-then-scan (`process.c:2511-2517`).
2. **Memory.** DMA buffers are contiguous, QEMU-visible (`astra_dma_data`
   `astra68.c:2005-2015`), <= 8 MiB, R|W only, owner-checked
   (`process.c:1864-1952`, `dma.c:376`). **Decision:** the app creates the
   DMA buffer itself (header with play/return rings + N buffers, N =
   max(3, latency/period+2), ~10 ms periods) under an audio capability the
   media service grants (control plane); close tells QEMU to stop before
   `dma_end`, as host channels do (`process.c:1498`).
3. **Daemon access.** Guest RAM is private anonymous
   (`run-arty.sh:251-252`); socket per command today (`astra68.c:3448-3642`).
   **Decision:** a file-backed shared *audio mailbox* (display mailbox
   pattern, `astra68.c:6656-6683, 1025-1061`): QEMU's stream kick copies the
   queued buffer from guest DMA into the stream's input ring there (Haiku's
   single copy into the mixer's per-input ring) and futex-wakes the daemon.
   No partial copies; versioned header.
4. **Clock.** No FIFO IRQ; feed every 2 ms (`astra_audio_host.c:479-503`,
   regs 45-61). **Decision:** daemon publishes a seqlock {frames_played =
   mixed_frames - level, monotonic_ns, rate} and per-stream consumed
   positions; QEMU times period boundaries from it (vblank pattern,
   `astra68.c:5157-5205`), advances return rings, raises the stream IRQ --
   one guest wake per period. Feed thread: absolute `clock_nanosleep`,
   SCHED_FIFO (lift `RestrictRealtime`).
5. **Counters.** One increment of `context_switches` (`process.c:1317`);
   `voluntary_switches` counts only YIELD (10992); idle-resume/exit have no
   cause; preemption causes overlap (1473-1485, 8166-8170). **Fix:** pass a
   single cause into `activate`, one counter each (block, yield, quantum,
   deadline, preempt, exit, idle-resume), `wait_blocks` only when a switch
   follows; test that causes sum to `context_switches`; astra-top "blocked"
   points at the new counter.

Order: counters (5), then the audio stream object + QEMU audio mailbox +
daemon clock, then SDL/pcm library on it, then media service to control
only and priorities (SDL audio thread 24, media 16).

Then phase 1 (audio) per the design; measure with `de25-ab.sh` against a
release published from `main` first.

## Research facts (file:line), kept for the implementation

### Haiku VIDEO (report)
- app_server: no compositor; all windows draw into ONE RAM back buffer (MallocBuffer) then CPU-copy dirty rects to framebuffer (HWInterface.cpp:339-406). Thread per window at B_DISPLAY_PRIORITY(15) (MessageLooper.cpp:47).
- Link batching: ~2 KB buffer of commands per write_port (LinkSender.cpp:50,96-103,424-460); drawing async (no reply); Sync()/DrawBitmap = round trip, DrawBitmapAsync = one-way (View.cpp:3058-3077,3126-3163).
- BBitmap: pixels in area shared app<->app_server (ClientMemoryAllocator.cpp:271; Bitmap.cpp:1148-1179 clone_area). App writes directly; DrawBitmapAsync message ~50 B = token+rects; server: bitmap->backbuffer memcpy, backbuffer->front copy. No vsync (B_WAIT_FOR_RETRACE commented out, ServerWindow.cpp ~2708).
- Retrace: client gets retrace sem once (AS_GET_RETRACE_SEMAPHORE), then acquire_sem locally per frame, released by driver vblank IRQ (PrivateScreen.cpp:268-286,717-731; intel_extreme.cpp:72-84).
- Overlay bitmaps (video players): accelerant buffer, lock sem shared (BitmapManager.cpp:96-141, Overlay.cpp).
- Cursor: event loop prio 90, cursor loop 95 (EventDispatcher.cpp:311-322); hw cursor hooks.
- Priorities: NORMAL 10, DISPLAY 15, URGENT_DISPLAY 20, REAL_TIME_DISPLAY 100, URGENT 110, REAL_TIME 120 (OS.h:325-339).
- SDL2 Haiku backend not in tree; from memory BWin=BDirectWindow + BBitmap framebuffer; must verify vs SDL source.

### Haiku AUDIO (report; paths ~/Git/haiku)
- App side: BSoundPlayer creates SoundPlayNode IN APP PROCESS (SoundPlayer.cpp:781); control thread "<name> control" at B_URGENT_PRIORITY 110 (SoundPlayNode.cpp:119; MediaEventLooper.cpp:443) calls app callback which writes in place into shared BBuffer (SoundPlayNode.cpp:853; SoundPlayer.cpp:934-943).
- Timing: clock-driven, no polling. Next buffer at fStartTime + frames_sent/rate (SoundPlayNode.cpp:709-712, no drift); thread sleeps read_port_etc until RealTimeFor(t, EventLatency+SchedulingLatency) (MediaEventLooper.cpp:258-260; MediaNode.cpp:366). Latency from downstream FindLatencyFor at connect (SoundPlayNode.cpp:436-453); late notices grow latency (<=30 ms app, <=150 ms mixer).
- Buffer: default 10 ms (MediaRoster.cpp:3394-3400); count max(3, latency/dur+2).
- BBufferGroup: one cloneable area (BufferGroup.cpp:64-91), registered ONCE with media_server (Buffer.cpp:233); SendBuffer = one write_port of id+header (BufferProducer.cpp:437-451; DataExchange.cpp:154). Recycle = release_sem via shared list, NO message (SharedBufferList.cpp:353-365). Backpressure: RequestBuffer acquire_sem timeout dur/2 -> skip, schedule advances (SharedBufferList.cpp:282; SoundPlayNode.cpp:842-851).
- Mixer in media_addon_server: control thread 120 queues buffer by start_time, copies/resamples into per-input float ring keyed by timestamp, recycles at once (AudioMixer.cpp:292-355,1181; MixerInput.cpp:149-150,263-298,694-730). Mix thread 120 timer-driven by hardware time source: nextRun = RealTimeFor(eventTime) - eventLatency - downstream (MixerCore.cpp:511-555), sends output buffer (MixerCore.cpp:563-738), zeros if no inputs.
- Output multi_audio (same process): output thread 120 blocks in BUFFER_EXCHANGE ioctl until HDA IRQ releases sem (MultiAudioNode.cpp:1765-1889; hda_multi_audio.cpp:1113; hda_controller.cpp:302-343); fills idle DMA half; double buffering, ~2048 frames default. It PUBLISHES THE TIME SOURCE (played_real_time, frames) -> shared area ring (MultiAudioNode.cpp:1809-1811,2090-2103; TimeSource.cpp:38-48,383-388,162-211). Everyone schedules in that hardware performance time.
- Per app buffer: 1 cross-process message (app->mixer). media_server: nothing per buffer. ~5-7 switches/period across 2 processes. Copies: ring, mix, output, DMA (4) all inside mixer process.
- Priorities: app producer 110, GameProducer 120, mixer/output 120, media_server 105. Haiku scheduler: >=100 real-time, no demotion, no budget (scheduler_thread.h:159-204).
- Game Kit: BStreamingGameSound = pull callback like BSoundPlayer; BPushGameSound = app cyclic buffer + memcpy.
### Astra AUDIO today (report; paths in repo)
- Path: SDL thread callback -> mixbuf -> astra_pcm_write memcpy to shared area (pcm_library.c:289) -> sync port exchange (send+wait+receive, 65-89) -> media single thread (main.c:581-626) memcpy area->host DMA span (main.c:484) -> host channel submit/kick (host_channel_client.c:91-114) + HOST_CHANNEL_WAIT blocks media -> QEMU vCPU MMIO kick (astra68.c:4964-5038) -> thread-pool worker -> astra_host_execute_audio sendmsg AF_UNIX seqpacket iov=guest RAM, blocking recvmsg 200 ms (3448-3642) -> daemon main thread epoll under ONE mutex, decode to float voice ring (astra_audio_host.c:1277-1308, 2569-2634) -> reply -> QEMU main loop completion IRQ_SRC_HOST (4764-4831) -> kernel ack-then-scan wakes media (process.c:2497-2548) -> media replies -> app.
- astra_pcm_wait: STATUS exchanges + sleep (frames-room)/rate + 1 ms (pcm_library.c:345-369): guest-paced, no hw clock.
- Per 1024-frame SDL buffer: ~3 exchanges (WRITE+2 STATUS), ~12 guest switches, 3 IRQs, 3 host commands, 15 host thread handoffs, 6 unix-socket msgs, 6 data copies (2 on 68040).
- Media: 1 thread, host channel command_capacity 1, serializes all streams; host completion not waitable (no wait_multiple).
- Daemon: feed thread nanosleep 2 ms polling FIFO level (512 frames, 10.67 ms), 2 MMIO writes per frame (407-503); no FIFO interrupt; CPUAffinity=3 (QEMU io threads too), Nice -5, RestrictRealtime=true.
- Primitives: areas (scattered pages; QEMU can't see), DMA buffers (contiguous, QEMU direct pointer astra_dma_data astra68.c:2005; creator-only), host channel ring (multi outstanding commands supported by QEMU pool; process/thread bound; not waitable), bulk rings (SPSC in area, user-space reserve/commit, RING_NOTIFY wakes, waitable endpoints, no watermark; guest-only), display mailbox (QEMU<->helper shared mem + futex precedent), scanout clock record (helper publishes vblank -> QEMU timer -> guest IRQ precedent for hw-clock wakeups), input shared seqlock (Haiku-modelled).
- Doc mismatch: host WRITE all-or-nothing (astra_audio_host.c:1290) vs doc "prefix accepted".
### Astra DISPLAY today (pending report)

### SDL2 Haiku backend (verified, beast ~/Git/SDL2/src/video/haiku)
- CreateWindowFramebuffer: BBitmap (shared w/ app_server), SDL pixels = bitmap->Bits() (SDL_bframebuffer.cc:44-78). Game writes directly.
- UpdateWindowFramebuffer: bwin->PostMessage(BWIN_UPDATE_FRAMEBUFFER); returns immediately (SDL_bframebuffer.cc:95-103). Game thread NEVER waits on present.
- Window thread: drops all other pending BWIN_UPDATE_FRAMEBUFFER (coalesce, latest wins) then SDL_BView::Draw -> DrawBitmap (SDL_BWin.h:452-462, 60-80). Only the window thread pays the Sync round trip.
- Lock: _buffer_locker around bitmap create/destroy only.
=> Pattern to copy: shared frame memory + async coalescing present + one server-side copy; game thread decoupled from display latency.

### Astra DISPLAY today (summary of the 2026-10-05 trace)
- Doom uses SDL's renderer: per frame LockTexture (private malloc copy,
  `SDL_astrarender.c:264-301`) -> `astra_surface_write` (AREA_COPY_IN +
  port call, `graphics.c:426-468`) -> 3 LIST_SUBMIT port calls (one per
  target change, `SDL_astrarender.c:916-1018`) -> PRESENT_DISCARD port call
  (`window.c:337`) -> vblank sync wait (`SDL_astrarender.c:1104-1107`).
- Display service, one thread, priority 20: `builder_begin` -> `settle()`
  waits the previous batch (`window_graphics.c:210-226`, `main.c:4106-4113`);
  present `render` settles then `submit_request` waits the compositor batch
  (`main.c:2570-2590, 2417-2424`); STATE event every present (4320-4330).
- Kernel one DMA in flight (`process.c:2944-2977`), Vesta queue depth 1
  (`platform.c:523-527`); QEMU memcpys batch + attachment on the vCPU thread
  in the MMIO store (`astra68.c:917-1019`); helper polls FPGA completion
  (`astra_graphics_hw.c:680-705`) and waits previous scene latch
  (`astra_terminal_display.c:2789-2808`).
- Per frame: 5 serialized device round trips, 11 cross-process messages,
  ~51 display-service syscalls; copies: 8->32 bit (Doom), AREA_COPY_IN,
  QEMU attachment memcpy, ~5x100 KB batch memcpys.
- Reusable: shared staging area; draw lists in shared area; 3 content banks
  per window (flip); posted latest-value word pattern (cursor 0x738);
  per-window vblank sync; panel-accurate vblank; DE25 host arena (zero-copy
  batch); unused `ASTRA_DISPLAY_FRAME_PRESENT_RGB565`. Missing: real fences,
  queue depth > 1, FPGA->helper interrupt.
