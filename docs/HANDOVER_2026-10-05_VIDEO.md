# Handover 2026-10-05: video, phases 2 and 3 done

Read `CLAUDE.md` (the **Haiku first** standing instruction), `AGENTS.md`,
then `docs/MEDIA_DATA_PLANE.md` -- the approved design; its Video section
items 4-6 are phase 3. `docs/HANDOVER_2026-10-05_MEDIA.md` holds phases 0
and 1; this page holds phase 2 (its phase 0 answers, decisions and board
result, below).

## State

- `main` is pushed. Phase 2 commits: `9b0b3935` (posted present, one
  submission a frame, a queue of two) and `ad5587aa` (system.library 3).
- **system.library is major 3** (`system.library.3`, ABI 3.0, one
  `ASTRA_SYSTEM_3.0` version node): `AstraDrawList` grew, so anything
  linked against `.2` no longer loads. Every in-tree program was rebuilt
  from wiped `build/` trees on beast and links `.3`; manifests require
  `system.library 3 3.0.0`.
- Versions that moved together: display host 1.1 (Vesta queue of two),
  ADLT 1.6 (TARGET), GUI protocol 18 (posted FRAME). The display mailbox
  (1.9) and the board helper did not change.
- The DE25 runs release `f216bdb2` (from `ad5587aa`), with `82bf10be`
  (phase 2 before the library bump, the A/B result below) beside it. The
  board keeps two releases, so `547e66fc` (phase 1) is gone: republish it
  from `28dab19c` to compare against phase 1 again.
- `verify-nopc.sh` on beast is green on `ad5587aa`, from a clean build.
- Board tip: a leftover `while pgrep -f de25-ab.sh ...` loop from an older
  session matched its own pattern and made a wait on the A/B look endless.
  Wait for the A/B's log, not with `pgrep -f de25-ab.sh`.

## Phase 3: what to do

From `docs/MEDIA_DATA_PLANE.md`, Video, items 4-6 ("copies"):

4. **Textures are written where the device reads them.** Streaming
   textures lock straight into the staging area (`ASTRA_LockTexture`'s
   ponytail note, `SDL_astrarender.c`), removing the private malloc copy
   and `AREA_COPY_IN`. The upload is then one more command of the posted
   frame, not a SURFACE_WRITE call -- which needs staging recycled the way
   lists are (the release event), or real fences (item 7).
5. **QEMU does not copy on the vCPU thread.** Today the running request's
   batch and attachment are copied in the MMIO store (`astra_display_start`
   when the queue is idle); a queued one's attachment is staged at submit.
   Hand the helper extents, or move the copy to the main loop.
6. **Present flips a content bank** with a posted latest-value word (the
   cursor's pattern), so a present is not a compose batch; the STATE event
   is already gone for posted presents.

Doom now runs at its own 35 Hz tic rate when idle, so it no longer shows a
display gain there: measure phase 3 in 125 Hz motion (30.9 presents/s,
guest idle 8%) and by guest idle and display CPU, or with a heavier
client. Phase 0 first, as before: file:line answers, then code.

## Phase 1 result (Chocolate Doom, de25-ab.sh, 3 rounds each)

| | before (`7387fd33`) | streams (`a9cf49d2`) | + priorities (`547e66fc`) |
|---|---:|---:|---:|
| presents/s idle | 18.6 | 21.4 | 21.2 |
| presents/s, 125 Hz motion | 13.9 | 15.9 | 15.3 |
| audio gaps in motion / 30 s | 6, 7, 1 | 2, 0, 0 | 0, 0, 0 |
| guest idle | 23.6% | 34% | 34% |
| cross-space switches/s | 807 | 331 | 334 |
| media service runs/s | 264 | 8 | 8 |

The priority change costs ~4% of frames in motion (SDL's audio thread at
24 preempts Doom's main thread as each buffer frees) and buys the last
gaps. Kept by the owner: gaps are what a player hears, and a busier game
would gap without it.

## Phase 2 as planned

Items 1-3 of the Video section: present posted, one submission per frame,
the display service pipelines. Done; the decisions taken are below
("Phase 0 answers"), and the measurement after them.

## How to measure

Publish from beast with `~/astra-mg/publish-prio.sh` (takes the build
lock; it builds, packs and deploys). The board keeps the current and the
previous release. Then on the board:

```
/var/lib/astra/current/bin/de25-ab.sh -n 3 <old> <new>
```

under `nohup`, output to `/data`. It interleaves the releases, restarts
Astra for each, opens Doom, and measures idle, 125 Hz motion and idle
again (presents/s, audio gaps, guest idle, switches by cause, per-process
CPU). `docs/DEBUGGING.md` section 7.

Gates prove a new path is taken, not only that it works: phase 1's audio
gates fail unless the voice is a stream; phase 2's Doom gate fails unless
frames are posted, a frame is one submission and the device queued a
request. Do the same for phase 3 (e.g. no SURFACE_WRITE per frame).

## Phase 0 answers (2026-10-05; file:line at `28dab19c`)

### How a present reaches the device today, and what blocks

Per Doom frame (`chocolate-doom/src/i_video.c:790-820`, smooth scaling on):

1. `SDL_LockTexture` returns a private malloc copy
   (`SDL_astrarender.c:264-287`); Doom converts 8 -> 32 bit into it.
2. Unlock -> `astra_surface_write` (`graphics.c:426-468`): `AREA_COPY_IN`
   into staging offset 0 (synchronous copy engine, `process.c:3053-3062`),
   then `SURFACE_WRITE`, a port call (`graphics.c:118-157`; every client
   call is `astra_port_call`, `port.c:126-174`, into a control port of
   capacity 1, `main.c:3978-3979`).
3. Three `LIST_SUBMIT` port calls, one per SDL flush: SDL flushes on every
   target change and on present (`SDL_render.c:2258, 4298`) and the
   renderer keeps one list per target (`SDL_astrarender.c:144-190,
   916-1018`). Doom's path is Clear(window) | SetTarget(upscaled) |
   Copy(tex->upscaled) | SetTarget(NULL) | Copy(upscaled->window) | Present.
4. `PRESENT_DISCARD`, a port call (`window.c:337-341`).
5. The vblank wait (`SDL_astrarender.c:1104-1107`).

In the display service (one thread, `serve_windows`, `main.c:4667-4783`):
every `builder_begin` calls `storage()` -> `settle()` (`window_graphics.c:
210-226`, `main.c:4106-4113`), which collects the previous batch, so each
of steps 2-3 waits for the one before it on the device. PRESENT renders
(`main.c:2570-2590`): settle, compose on the CPU, `present` ->
`submit_request`, which waits for its own completion (`main.c:2417-2424,
2454-2461`), and that completion can sit behind the previous present's
flip (`docs/PRESENTATION.md`, Bank retirement). Then a STATE event on every
present (`main.c:4320-4330`; PRESENT sets `changed`, `main.c:3113`), which
SDL turns into MOVED+RESIZED (`SDL_astravideo.c:378-404`). While the
service sits in `collect_request` it serves nothing, not even the vblank
IRQ, so the client's vsync wake is late too. One 8 MiB batch buffer serves
everything (`main.c:4816`).

### What a queue of two needs, layer by layer

| Layer | Single-slot today | Needed |
|---|---|---|
| Service | `in_flight {active, fence}` `main.c:2321-2328`; settle before every submit `:2398`; one batch buffer `:4816` | ring of in-flight requests by fence, each owning its batch buffer; two buffers; wait only for a buffer's own request |
| Kernel | `display_dma_token/owner/active`, one `display_attachment` `process.c:395-402`; refuses while active `:3292, :3324`; collect completes "the" token without a fence `:3379-3399` | a slot per request {fence, token, owner, attachment, area}; collect by fence; abort walks slots |
| Kernel platform | submit needs READY and not BUSY and no completion `platform.c:525-529`; collect fails if another completion remains `:587-588`; version 1.0 exact `:460-461` | submit on READY alone; accepted-count success test; collect succeeds with more pending |
| Kernel IRQ | Astraea completion requires `IRQ_STAT & IRQ_EN == 0` at ack `platform.c:941-942`, else quarantine `irq.c:1081-1099` | a DRAW_DONE raised again is new work, as storage's level is (`platform.c:906-914`) |
| Vesta | one REQ_* and one CPL_* set, QUEUE has BUSY/READY/CPL_VALID (`vesta.h:124-134`, `display.h:232-237`) | display host 1.1: held count and accepted sequence in QUEUE, completion FIFO behind CPL_*, DRAW_DONE level while completions are held |
| QEMU | `busy`, `completion_valid`, one triple `astra68.c:349-400`; refuses while busy `:974`; copies on the vCPU in the store `:1031-1043` | request FIFO and completion FIFO; start the next request from the completion handler on the main loop |
| Mailbox, helper | one request, one completion (`display_mailbox.h:39-56`); one 8 MiB payload | nothing (see decision 1) |

### How Haiku takes `DrawBitmapAsync` without a reply (`~/Git/haiku`)

The client appends `AS_VIEW_DRAW_BITMAP` {bitmap token, rects, options} to
its link buffer and flushes it with one `write_port` unless it is inside a
transaction (`View.cpp:3058-3077, 6813-6818`; `LinkSender.cpp:50, 96-103,
424-461`). `DrawBitmap` is the same plus `Sync()`, one round trip
(`View.cpp:3125-3163`, `Window.cpp:697-709`). The window's server thread
(`B_DISPLAY_PRIORITY`, `MessageLooper.cpp:47`) draws it straight from the
client's shared area (`ServerWindow.cpp:2681-2715`, `Bitmap.cpp:1160-1170`)
and never replies; `B_WAIT_FOR_RETRACE` is commented out (`:2707-2709`).
Back-pressure is only the window port's capacity of 100
(`ServerWindow.cpp:274`): a client that gets ahead blocks in `write_port`.
Drawing commands are never dropped; redraws are coalesced (`RequestRedraw`
sets `fRedrawRequested`, the loop clears it once, `ServerWindow.cpp:
378-386, 4319`). There is no fence: reusing a bitmap safely takes `Sync()`
or a second bitmap. Retrace is the driver's semaphore, acquired by the
client directly (`PrivateScreen.cpp:268-286, 717-731`). Latest-wins is
SDL's, in the window thread (`SDL_BWin.h:452-462`), not app_server's.

### Decisions

1. **The queue of two lives in the device model, not the mailbox.** The
   DE25 host arena is 8 MiB (`fpga/de25/linux/astra_host_arena_uapi.h:11`),
   exactly one batch, and the engine runs one batch at a time, rebasing its
   rings for each (`astra_terminal_display.c:900-917`). So Vesta holds two
   requests and QEMU hands the helper one at a time through the unchanged
   mailbox; the next starts from the completion handler on QEMU's main
   loop, with no guest round trip between them. A queued request's
   attachment is still copied when it is submitted (the client may reuse
   staging once SURFACE_WRITE is answered, `graphics.c:1208-1212`); its batch
   is read when it starts (the kernel holds the DMA until completion).
2. **Service:** two batch buffers, in-flight requests by fence, each
   holding its buffer; `builder_begin` waits only when its buffer's request
   is still running. A present does not wait for its completion: the
   service commits its render state at submit, which the bank rules allow
   because the device runs requests in order (`docs/PRESENTATION.md`, Bank
   retirement). READ_SURFACE and cursor images wait for their own fence.
3. **One submission per frame:** ADLT 1.6 adds `TARGET`, a command that
   changes the destination of the commands after it. The renderer keeps
   one list; a target change appends a mark. The service replays the
   segments into one batch, switching destination descriptor at each mark,
   and validates clips against the current destination.
4. **Present is posted:** a new graphics action `FRAME` {list, PRESENT,
   DISCARD} is sent with no reply capability. The list is recycled through
   an auto-reset event the client hands over at LIST_ATTACH and the service
   signals once it has replayed the list -- Haiku's buffer recycle
   (`SharedBufferList.cpp:353-365`), one buffer because the service
   (priority 20) replays on arrival. A failed frame is logged and dropped,
   as an async draw is. Composes coalesce: a window whose port holds more
   messages is composed after them (Haiku's `fRedrawRequested`). A posted
   present sends no STATE event; nothing in the window's state changed.
   SDL posts its pending list before any synchronous call (texture update,
   read, destroy), so order holds.
5. **Not in this phase:** uploads stay one SURFACE_WRITE call (it no longer
   waits for the device); real fences and the copies are phases 3 and 4.

Per Doom frame after phase 2: one SURFACE_WRITE call, one posted FRAME,
one vblank wait; the device runs upload, frame and compose back to back.

## Phase 2 on the board (Chocolate Doom, de25-ab.sh, 3 rounds, 30 s windows)

Release `547e66fc` (phase 1) against `82bf10be` (this phase):

| | `547e66fc` | `82bf10be` |
|---|---:|---:|
| presents/s idle | 20.4-21.3 | 34.9-35.1 |
| presents/s, 125 Hz motion | 15.2-15.5 | 30.5-31.5 |
| guest idle (idle / motion) | 34-36% / 32-33% | 18-21% / 8% |
| audio gaps | 0, 0, 2 idle; 0 motion | 0 |
| cross-space switches/s idle / motion | 322-336 / 360-364 | 306-312 / 411-418 |
| cursor in motion | 52/s, max 50-67 ms | 59/s, max 34 ms |

The guest's idle time became frames, as predicted. Idle is now at Doom's
own 35 Hz tic rate; motion doubled. Cross-space switches in motion rise
with the frame rate (about the same per frame).

Gates: the Doom gate fails unless the display logged a posted frame, the
device saw at most 2.5 render-only batches per compose (2.00 measured; the
old three-flush path is 4.00 -- perturbed and seen to fail), and a request
was queued behind a running one. verify-nopc.sh is green.

Next: phase 3 (copies: textures locked into staging, QEMU not copying on the
vCPU, present as a bank flip). Doom is at its tic cap, so measure phase 3 on
motion and on guest idle, or with a heavier client.

## Phase 3 (2026-10-05): item 4 done, items 5 and 6 dropped by the owner

### Phase 0 answers (file:line at `eb4778ce`)

- A Doom frame's texture went through two CPU copies and one call:
  `ASTRA_LockTexture` handed out a private malloc copy
  (`SDL_astrarender.c:278-301`, the ponytail note at :285); unlock called
  `astra_surface_write` (`graphics.c:435-476`), which packed it into
  staging with the copy engine (`AREA_COPY_IN`, :472, a memmove in the
  vCPU's MMIO store, `astra68.c` `astra_copy_run`), then sent SURFACE_WRITE,
  a port call (:476). The service built one upload batch per band with the
  staging rows as its attachment (`window_graphics.c:374-431`).
- The kernel already hands the device the staging area by extents and holds
  it until the batch is collected (`process.c:3229-3274`). QEMU takes the
  rows when it accepts the request -- into the payload when the queue is
  idle (`astra68.c:1063-1120`, on the vCPU in the store), into a staged copy
  when it is queued (:830-839, :1198) -- so staging is the client's again
  once the request is accepted, which is before the service signals a
  posted list's release. That is the recycling item 4 needed: no fences.
- **Board profile, Doom on `f216bdb2`** (profiling QEMU built with
  `emu/qemu/build.sh de25-profile`, run through a runtime drop-in;
  `~/astra-mg/doom-prof/p3-board.aprof` on beast): the display service is
  16-20% of guest time but 0.8-1.0% of guest instructions; the kernel is
  8-11%. `astra-top --perf` on the vCPU thread: 85% translated code, TB
  lookup ~11%, TLB flush 2-3%, `memcpy` below 0.9%. So QEMU's copies do
  not cost the vCPU (item 5), and the compose is bounded by the display's
  ~1% of instructions (item 6). The display's time goes per run, not per
  instruction: what a run costs is not yet attributed (cold TLB and TB
  lookups after a cross-space switch are the suspect).

### What landed

- ADLT 1.7 adds `UPLOAD` (11): rows of the window's staging area into a
  CPU-writable surface, in order with the frame's draws
  (`docs/MANAGED_GRAPHICS.md` section 4). The service ends the batch before
  it and uploads exactly as SURFACE_WRITE does (`list_upload`,
  `upload_rows` in `window_graphics.c`).
- system.library 3.1.0 (ABI 3.1, node `ASTRA_SYSTEM_3.1`, additive):
  `astra_draw_upload` appends an UPLOAD; `astra_display_stage` copies caller
  rows into staging at an offset with the copy engine.
  `astra_surface_write` is now built on it. SDL.kit requires 3.1.0.
- SDL: a lock is rows of staging (Haiku: the game draws into the
  `BBitmap`, `SDL_bframebuffer.cc:44-78`); unlock appends the UPLOAD. A
  frame's uploads lie side by side; staging starts over once the list is
  back. A second lock that does not fit beside an open one is a private
  copy uploaded at unlock; a readback while a lock is open fails, because
  readback returns through staging.
- Per Doom frame now: one posted FRAME (its upload inside), one vblank wait.
- Gate: the Doom gate fails unless the service logs a frame upload and the
  copy engine runs under once per four composes (0 in 1051 measured;
  forcing the private-copy lock gives 1051 in 1051 and fails).
  `verify-nopc.sh` green.

### On the board (de25-ab.sh, 3 rounds, `f216bdb2` against `8c1ce754`)

| | `f216bdb2` | `8c1ce754` |
|---|---:|---:|
| presents/s idle | 35.0 | 35.0 (Doom's cap) |
| presents/s, 125 Hz motion | 30.4-31.4 | 32.1-32.7 |
| guest idle (idle / motion) | 19-21% / 8% | 22-24% / 9-10% |
| display runs/s idle | 127 | 100 |
| display CPU idle / motion | 15.7% / 19.9% | 14.4% / 18.9% |
| Doom CPU idle | 62.6% | 60.6% |
| cross-space switches/s idle / motion | 309 / 413 | 252 / 382 |
| audio gaps | 0 | 0 |

One display run a frame fewer costs ~1.3 points of display CPU: about
0.5 ms of guest time per run for a few thousand instructions.

### Dropped

Items 5 (QEMU copying off the vCPU) and 6 (present as a posted bank flip)
were dropped by the owner on these measurements. Item 7 (real fences) stays
for whoever needs upload completion; nothing in-tree does today.

Next: SDL2 mouse, in full (cursor show/hide, custom and system cursors,
warp, relative mode, capture, global state).
