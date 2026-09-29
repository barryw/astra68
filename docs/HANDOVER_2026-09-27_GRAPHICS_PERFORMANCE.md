# Handover 2026-09-27: graphics performance — find and remove every bottleneck

Read `CLAUDE.md`, `AGENTS.md`, then this page. The previous handover
(`docs/HANDOVER_2026-09-26_SDL_HARDWARE.md`) covers service tiers, the storage
hang, and everything deployed through release `25727bf9…`.

## The bar (user, non-negotiable)

- **60 fps on screen with ten TestDraw2 windows open, zero slowdown.** The
  number of applications must not matter. DevilutionX and other games are the
  reason.
- A 70 MHz 68040 only feeds commands; the FPGA does all pixel work. **The
  hardware must never be the bottleneck**, and the 040 should be near idle
  while TestDraw2 runs.
- **Never wait on vblank to compute anything.** Only the change of what is on
  screen waits for vblank, and only when a previous change is still in flight.
  The OS, the renderer and the hardware run as fast as they can.

## Board state right now (astra-de25, via beast)

- Release `25727bf9…` is installed and current. **Runtime overrides are
  active** (systemd manager environment, lost on reboot):
  `ASTRA_TERMINAL_DISPLAY=/data/cert/astra-terminal-display.async` and
  `ASTRA_DISPLAY_PROFILE=1` (per-batch `display profile` / `render profile`
  lines in `journalctl -u astra`). Clear with `systemctl unset-environment`.
- Restarting Astra kills QEMU, which dirties ext4. Before every restart, remove
  `/var/lib/astra/state/<release>/storage-terminal.img` so the launcher copies
  a fresh one:
  `systemctl stop astra-remote-desktop astra; rm -f $S/storage-terminal.img;
  systemctl start astra; sleep 45; systemctl start astra-remote-desktop`.
- Board tooling on beast, in `~/astra-mg/hw/`:
  - `board_e2e.py` (phases `before` / `after`), `board_stress.py`,
    `board_fps.py SOCK N` (opens N TestDraw2 windows, then prints
    submissions/completions/render-only/blits per second), `board_run.py`
    (runs shell commands, e.g. `ps`), `board_tail.py`, `board_launches.py`.
  - Forward QMP first:
    `ssh -N -L /tmp/de25-qmp.sock:/run/astra/qmp.sock root@192.168.1.52 &`.
  - Desktop icon centres: Gallery (72,112), Terminal (72,224),
    TestDraw2 (72,336).
- Profiling on the board: `perf` is at `/usr/lib/linux-tools-5.15.0-191/perf`
  (`record -g -p <pid>`); `strace -c -p` also works. Inside the guest, `ps`
  gives per-process CPU% and TIME.

## What was measured (TestDraw2 open on the DE25)

| Stage | Batches/s | Helper CPU | Notes |
|---|---|---|---|
| Release 25727bf9 | 17 | 56% | 89.5% of helper time in `astra_window_scene_compile` |
| + compile/mapping/poll fixes | 47 | 33% | FPGA time now dominates |
| + vblank never blocks (current) | 48 | 25% | present 54 µs, was 5–7 ms |

Per batch now:
- **TestDraw2 frame:** ~510 commands, 102 KB, **15–17 ms of FPGA time**
  (~3000 cycles per small command at 165 MHz).
- **Compose:** 15 BLITs, which together are one full-window content→cache
  copy split around the rounded bottom corners. **12 ms of FPGA time**.

The FPGA is busy about 70% of every second, so the render engine is now the
ceiling. QEMU (the whole guest) is at 56% of a core. Guest CPU per process
has not yet been profiled against the new rate.

## Fixes made this session (uncommitted, host-tested, running on the board)

All in `fpga/arty/linux`:
1. `astra_window_scene.c`:
   - The compile builds into cached memory, not byte stores into the uncached
     `/dev/mem` arena.
   - Layers are decoded once, not per line.
   - The helper publishes the scene as: invalidate the header, copy the body,
     then the header.
2. `astra_graphics_hw.{c,h}`: the whole arena is mapped once per device, and
   `memory_map_open` returns views. It used to mmap/munmap on every batch.
3. All six hardware polls sleep 50 µs, not 1 ms.
4. Vblank decoupled:
   - `astra_graphics_scene_commit_begin` / `_wait`. `present` and
     `present_window_scene` issue the commit and return.
   - `finish_pending_present` runs only before a request that changes the
     screen. Render-only batches, surface reads and cursor requests never
     wait.
   - Cursor commits are issued, not awaited; `wait_pointer_ready` orders
     them.
   - Internal callers that draw into what was on screen use `present_now`.
   - Guarantee to the guest: when a screen-changing batch runs, every earlier
     present is on screen.

To ship them: `emu/qemu/publish-de25-release.sh` (it rebuilds
`build/de25-graphics/linux/astra-terminal-display`), deploy, then clear the
overrides.

## Work in flight when context ran out (subagents; they will not survive)

- **RTL render-engine analysis:** fpga-agent, briefed to prototype on a
  worktree branch or under `beast:~/astra-mg`, never in the main tree.
  - Task: measure cycles per command and per pixel in sim, with realistic
    DDR latency, for tiny commands, large copies and fills; find the
    structural causes (outstanding AXI reads, bursts, read-modify-write,
    blocking completion writes, command fetch and serialization); rank fixes
    by speedup per ALM against the 875 free ALMs; prototype the best, keeping
    the sim suite bit-exact.
  - Check `git worktree list` and `beast:~/astra-mg` for its branch or
    notes. If nothing is there, redo it.
- **Display service, option B (approved):**
  - Double-buffered content surfaces that the scene shows directly: an A/B
    layer split gives pixel-identical rounded corners; content pitch rounded
    to 64.
  - Copy-forward of previous damage by default, and an opt-in discard flag on
    PRESENT that the SDL renderer sets. That is a GUI protocol version bump,
    with the NDK, SDL, docs and `test_display.c` updated together.
  - The menu overlay no longer redraws on every compose.
  - Result: an SDL window's compose emits 0 GPU commands, versus 15 BLITs
    today.
  - Check `git status` for partial edits in `sw/userspace/services/display`,
    `sw/include/astra/gui.h` and `ndk/`, and finish or redo it.
- The storage power-cut gate (`emu/qemu/test-storage-power-cuts.py`) was
  started on beast with `~/astra-mg/pc/{rom.bin,base.img}` (image includes
  the `posix` test command). Log: `/tmp/v-pc.log`. It is exhaustive and takes
  hours; check it.

## Plan to reach the bar, in order

1. **Render engine RTL.**
   - Target ~1 px/clock for fill and plain copy, and per-command overhead of
     tens of cycles.
   - Reclaim ALMs if needed; the texture engine's six 50-bit attribute
     steppers were suggested.
   - Prove it in sim, then route the DE25 shell (`fpga/de25/build_astra_shell.sh`),
     install, certify with `astra-render-certify`, and record in
     `fpga/de25/TIMING_CLOSURE.md`.
2. **Finish option B,** so composes cost nothing for SDL windows.
3. **Batch SDL primitives:**
   - Points and lines must not be one command each. Add multi-point and
     polyline commands (or fold them into triangles) in the protocol JSON,
     the RTL and `render_builder.c`, with the reference model updated.
   - Target: a TestDraw2 frame in a handful of commands.
4. **Pipeline the transport.** One mailbox slot serializes everything (guest
   submit → helper → FPGA → completion IRQ → next submit):
   - Let the helper queue several batches, and let the guest submit while
     the FPGA runs.
   - Avoid copying batches: FPGA-visible staging instead of copying the whole
     batch into the arena each time.
   - Measure the round trip.
5. **Profile the guest at speed:** with 10 TestDraw2 windows, where does the
   040 go (`ps`, kernel perf counters, QEMU `-d` if needed)? Look at draw-list
   building, the per-command validation in the display service, and syscalls
   per frame. The 040 must be near idle.
6. **Acceptance:** 10 TestDraw2 windows, a 60 fps screen (composes landing
   every vblank), each app at its own full rate, and the 040 idle headroom
   measured. Then DevilutionX.

## Other open items (from the previous handover)

- The zsh crash at pc 0 when its display goes away.
- Commit everything in logical chunks once gates are green. Nothing from
  2026-09-25 onward is committed.
