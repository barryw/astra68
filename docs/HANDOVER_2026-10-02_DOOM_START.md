# Handover 2026-10-02: why Doom takes so long to start

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-01_LAUNCH.md`.

## The answer

Doom's start on the DE25 is **emulated CPU work**, not waiting. The A76
cores run at 1.4 GHz (no cpufreq driver), and TCG executes this workload at
about 18 MIPS. Measured under the instruction-counting plugin, on beast and on
the board alike, the start cost about **300 M guest instructions** and only
**5% of them were Doom's own code**:

| Where | Share |
|---|---|
| kernel (syscalls, waits, IPC, context switches) | 61% |
| storage service (lwext4) | 17% |
| runtime memcpy/memcmp | 7% |
| Doom | 5% |
| SDL | 3% |

Average kernel cost per syscall: about **3,100 guest instructions**.

Phases on the board before this work: launch 1 s, `I_Init` 5.1 s, `M_Init` +
`R_Init` 3.2 s, `S_Init` + precache 6.4 s, the rest 2 s.

## Done and committed (full verify green, `EXT4=0` now in verify)

- `98e4665d` storage: a missing name costs one round trip. Doom's music-pack
  probe makes ~480 failed `fopen` calls, each about 200,000 instructions:
  - lwext4 patch 0021: metadata checksum verifies only fed a debug warning;
    with debug output off they are compiled out (`CONFIG_META_CSUM_VERIFY`).
  - lwext4 patch 0022: type mismatches answer `EISDIR`/`ENOTDIR`/`ELOOP`
    instead of `ENOENT`, so libc no longer stats after a miss and the VFS
    client no longer re-walks the path on `NOT_FOUND`.
  - Profile: 300 M -> 222 M guest instructions.
- `f7fb74c6` `run-arty.sh` forked `sed` and `sleep` ~80 times a second:
  a fifth of all four DE25 cores, permanently. Now fork-free, 0.5 s poll.
- Board, release `2ecc70f3`: **Doom window at 13.7 s** (was 18.3-19.5 s).
  `I_Init` 1.3 s (was 5.1 s).

## Next, in order (largest first)

1. **`S_Init` precache, ~5.7 s.** Every effect is one `astra_pcm_convert`
   session draining 8 KB per exchange: ~1,550 IPC round trips through the
   media service and the host channel. The 8 KB is
   `ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_MAX_FRAME_BYTES`, shared by
   `pcm_library.c`, the media service's single host client window
   (`astra_host_client_open`) and the Linux audio host. A convert area sized
   for a whole effect (a few hundred KB), or one convert session SDL keeps
   open per format pair, makes it one or two exchanges per effect. Check the
   host-channel window limit first.
2. **Kernel cost per syscall (~3,100 instructions).** Validation-heavy hot
   path: `thread_at_slot` ~19 calls per syscall, `registration_at`,
   `process_for_thread`, `valid_wait_queue_header`, `kernel_stack_valid`,
   `capture_current`. This is the multiplier on everything. Needs cycle
   budgets per `AGENTS.md` before changing.
3. **`M_Init`/`R_Init`, ~3.2 s.** ~550 `W_ReadLump` reads, each a VFS
   round trip, 70% kernel. Larger reads or a mapped WAD.
4. `media` and `remote-desktop` exit 16 and are relaunched every few
   seconds under plain QEMU (no host helpers). Not fatal; floods the trace
   ring. Unexamined.
5. The A76 clock is 1.4 GHz; the part allows more. Firmware, not guest.

## Tools (beast `/tmp`, not kept)

- `doom_startprof_qemu.py QEMU PLUGIN ROM IMAGE OUT`: profiling QEMU with the
  Doom gate's stand-in helpers, one interval per Doom start-up phase. Use
  the current `build-host-profile-*` build: an older one lacks the audio
  host and Doom then starts without sound, which hides `S_Init`.
- `doom_startprof_board.py`: the same on the board, through a temporary
  `astra.service` drop-in that sets `QEMU=` to the `de25-profile` build and
  adds `-plugin ...control=/run/astra/prof.sock`. Remove the drop-in after.
- `ASTRA_DISPLAY_STALL_DUMP=/data/stall` in a drop-in captures a batch the
  board refuses (see `CLAUDE.md`).
