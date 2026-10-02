# Handover 2026-10-02: repeated and thrown-away work

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-02_DOOM_START.md`.

## Result

Chocolate Doom start under the profiling QEMU (`doom_startprof_qemu.py`,
exact MC68040 instruction counts, beast):

| Phase | Before | After |
|---|---:|---:|
| launch | 54.5 M | 20.5 M |
| I_Init | 24.7 M | 26.3 M |
| M_Init | 36.7 M | 29.3 M |
| S_Init | 80.8 M | 33.8 M |
| CreateUpscaledTexture | 23.0 M | 23.8 M |
| **total** | **222.2 M** | **136.7 M** |

Full `verify-nopc.sh` green; K1 qualification (performance budgets) green.

**DE25, release `74270915`**: Doom renders **10.1-10.4 s** after the
double-click over three runs from a fresh `astra.service` (was 13.7 s on
`2ecc70f3`). `S_Init` + precache ~3.4-3.9 s (was 6.4 s); `M_Init` + `R_Init`
~1.9 s (was 3.2 s). Measured with `doom_timeline.py` over a QMP forward.

The first release (`3275f18f`, without the slice fix below) rendered at
9.8-10.2 s but the audio host's FIFO underruns rose from ~2,000 to ~29,000
frames per session: the single-threaded audio host resampled a whole 64 KiB
CONVERT reply between FIFO feeds. It now produces the reply 8 KiB at a time
and feeds the FIFO between slices (`CONVERT_SLICE_BYTES`); underruns are back
to 1,400-1,900 per session. The audio host's mixer sharing one loop with
request work is the real limit -- a feed thread would remove it.

## Round 2 (same day)

**DE25, release `e4d93ead`: Doom renders 7.5-8.3 s after the double-click** (13.7 s at the
start of the day). Guest instructions for the start: 121 M (222 M before).

How the remaining time was found: guest-PC sampling on the board showed the
vCPU **idle ~55% during S_Init** -- waiting on host commands. QOM counters
(`astra-host-commands`, `astra-host-execution-ns`) put 2.65 s of the start
in host execution, and the audio host's new per-operation report (printed at
exit, `ASTRA AUDIO HOST operation=N count= total_us=`) put it in CONVERT.

- **The audio host ran on a Cortex-A55.** CPUs 0-1 are A55, 2-3 A76; the
  vCPU is on 2 and QEMU's host I/O threads on 3 (`astra-input-hotplug.py`).
  The audio host was pinned to CPU 0, where resampling is 3.5x slower. Now
  `CPUAffinity=3`: CONVERT 0.80 s -> 0.24 s per Doom start.
- **Resampler** tabulates interpolated taps per output phase (Doom's 4:1 has
  4 phases), bit-identical output, 449 -> 231 ms per minute of audio on an
  A76; Kaiser tables cached per rate pair (61 ms per start on the A76).
- **Audio host feed thread**: the 512-frame FIFO (10.7 ms) is fed while the
  main loop serves a request; CONVERT resamples unlocked.
- **POSIX ports get compiler builtins** (`-ffreestanding -fno-builtin` was
  inherited from the native contract): Doom's dehacked setup made 433,000
  `memcmp(p, q, 4)` library calls. Doom is compiled `-O2`. `memset` uses
  `movem`. 137 M -> 122 M instructions.
- **Kernel COW bug** (the "something depends on zeroed malloc" item):
  `kernel_vm_private_commit_range` refused a kernel store into a page shared
  copy-on-write since fork; now it breaks COW. The heap no longer zeroes
  retained extents. `fix(posix)`: `accept()` read the listener after the
  socket table could move.

### Next

1. The QEMU host-I/O core (CPU 3) now also runs the audio host, including
   FluidSynth for music; watch display/input latency under music.
2. 1,100 host commands per Doom start are audio WRITE/STATUS at ~0.05 ms in
   the daemon; the per-command cost is transport (thread pool, socket,
   completion IRQ), ~0.2 ms each on the board.
3. Kernel wait path and `astra_clock_monotonic` syscalls remain as listed
   below.

## What changed, and the work it stops repeating

1. **PCM transfers in 64 KiB, not 8 KiB** (`ASTRA_PCM_TRANSFER_BYTES`).
   Doom converts every effect 11025 Hz U8 mono to 44.1 kHz S16 stereo, 16x the
   bytes, and the output drained 8 KiB per IPC + host-channel round trip.
   Converter and MIDI sessions now move 64 KiB per exchange; the media service
   bounds every request by the area the client mapped (`session->area_bytes`);
   a converter's area is sized to the job, so a short effect is one exchange.
   Voices keep their 8 KiB batch. Host packet limit follows
   (`ASTRA_AUDIO_HOST_PACKET_BYTES`, audio host protocol 6) in QEMU, the Linux
   audio host and the gate stand-in.
2. **The heap keeps freed pages** (`ASTRA_ALLOCATOR_RETAIN_PAGES`, 256).
   Every free over 2 KiB decommitted at once, so the next similar malloc
   faulted, kernel-zeroed and later unmapped (with a `cpusha` per page) the
   same pages again. Freed extents are now retained committed up to 1 MiB and
   **zeroed when reused**. The zeroing is required: with stale contents the
   terminal gate's `posix -R` hung, so some consumer depends on large mallocs
   reading zero (unidentified; see Next).
3. **lwext4 reads ahead** (patch `0023-file-read-ahead.patch`). A file-data
   miss read one 4 KiB block, ~30 syscalls through the block lease each. A
   miss now reads up to 16 physically contiguous file blocks in one transfer,
   by a new scatter read (`breadv` -> `astra_block_readv` -> lease/memory
   backends) straight into cache buffers. Mount test: 31 blocks in 1000-byte
   pieces = 2 device reads (31 before).
4. **Block lease lane lock is a user-space mutex.** `state_lock` was a kernel
   semaphore taken ~6 times per request: a dozen syscalls per block request.
5. **Kernel hot path:**
   - port receive cleared a 1.3 KiB `KernelPortReceipt` (embedded 255-entry
     import arrays) twice per receive; now field resets.
   - host-channel completion IRQ scanned all 256 slots; now a waiting bitmap.
   - wait-registration ids: shift, not divide by 255, on every queue hop;
     `registration_thread` does one validated lookup instead of two.

## Next, largest first

1. **Find the large-malloc-is-zero consumer.** Poison retained extents
   (0xA5) instead of zeroing, run `test-terminal.py`, find what breaks. Then
   decide whether zero-on-reuse stays (it costs a memset per reuse).
2. **Kernel wait path** (still the top kernel cost): wait-queue links are
   slot-encoded ids re-validated on every hop (`registration_at`,
   `valid_wait_queue_header` ~8 per blocking wait); `wait_handle_set` looks
   each handle up three times; `capture_current`/`runtime_resume` re-derive
   the process. Make links pointers, validate once, keep audit under
   `KERNEL_AUDIT`.
3. **`astra_clock_monotonic` is a syscall** everywhere (media health loop,
   block metrics twice per request, SDL ticks). A user-readable timebase would
   remove all of them.
4. VFS client uses send + wait + receive (3 syscalls) per request; a
   `PORT_CALL` path is 1.
5. Supervisor stacks: thread creation zero-allocates then poison-fills 8 KiB,
   and reaping re-poisons a stack about to be freed.
6. Re-measure Doom start on the board (release + `doom_startprof_board.py`).
