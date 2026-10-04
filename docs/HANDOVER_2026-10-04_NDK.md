# Handover 2026-10-04: NDK cleanup, posted cursor, examples gate

Read `CLAUDE.md`, `AGENTS.md`, then this page. Phases 1-4, the posted
cursor and the SDL/errno fixes are commit `34d137ef` (full beast verify
green). Phase 5 is the commit after it.

## Owner decisions taken this round

- `ndk/` holds only public headers, docs, examples, make fragments and
  distribution tests. Implementation moved out (see below).
- Dead/stub API is deleted, not kept. Real code that no dynamic program could
  call is exported instead.
- Every symbol a shipped library exports is public API with an NDK header.
- Examples must be complete programs, linked from the dist archive and run in
  a QEMU gate (phase 5, done).
- Quake port is parked; its own decisions are recorded at the end.

## Done (locally; verified as stated)

**Phase 1 -- relocation.** `ndk/src` -> `sw/userspace/system` (new component:
`Makefile`, `system.library.exports`, `src/`, `src/internal/`, `tests/`).
Outputs keep their names (`libastra.a`, `libastra-pic.a`,
`system.library.2`). `utf8.c` -> `sw/common/utf8.c` (kernel, runtime, system
compile it; runtime as `common_utf8.o`, kernel as `utf8.o`). Front panel
(MMIO, boot-only) -> `sw/boot/front_panel.c` + `front_panel_mmio.h` /
`front_panel_memory_map.h`, header `sw/include/astra/front_panel.h`, host
test `sw/boot/test_front_panel.c` in `make -C sw/boot test`. Test seam
renamed `ASTRA_NDK_TEST` -> `ASTRA_SYSTEM_TEST`, `astra_ndk_*` ->
`astra_system_*`. All consumers repointed (libraries.mk owners, top
`sw/userspace/Makefile` SUBDIRS now `compiler runtime system ...`, vfs
`SYSTEM_STATIC_OBJECTS`, supervisor, storage, config, graphics, interface,
input, terminal, terminfo, kits, clipboard, tools, fpga/arty/linux,
publish script). `test_boundaries.py` updated and passing.

**Phase 2 -- dead API.** Deleted: `font.h` + `font.c` (face/layout stubs;
`ASTRA_FONT_BITMAP_MASK1/A8` moved to `font_library.h`), `fixed.h`/`fixed.c`,
graphics stubs (`astra_display_open`, `graphics_present/get_info`,
`set_mode`, `set_pointer_*`, circle/ellipse/pattern/flood/text_layout,
palettes, sprite sets, raster programs, `present_surface`, `get_status` and
their types), `astra_datetime_format`, `AstraFixed26_6`,
`AstraAcquireOptions` (moved to firmware `front_panel.h`). Examples
`font_layout.c`, `front_panel.c`, `graphics_frame.c` removed. None of the
deleted functions was exported, so `system.library` stays major 2: ABI 2.8
**adds** bulk rings (12), `astra_datetime_now/unix_seconds`,
`astra_keymap_is_modifier/apply_modifier`. Runtime.kit provides
`system.library 2 2.8.0`.

**Phase 3 -- headers.** `ndk/Makefile` `KERNEL_ABI_HEADERS` is the curated
`sw/include` ship list (30; private wire formats no longer ship).
`header-contract` compiles every shipped header alone as C11 and C++17
(perturbed: fails without `compiler.h`). New `sw/include/astra/object_abi.h`
is the single definition of rights, area flags, bulk-ring constants and
header (the drifted copies in `syscall.h`/`resource.h`/`area.h`/
`bulk_ring.h` are gone). `compiler.h` is assembler-safe and included by every
ABI header that uses `_Static_assert`; QEMU `prepare-source.sh` copies
`compiler.h` and `object_abi.h`. Public now: `vfs_reader.h` (read sources +
`astra_process_library_source_open`; resolver stays private in
`vfs/include/astra/vfs_library_source.h`), `ping.h`, `ntp_client.h`.
`bytes.h` C++-safe, declares `strcpy`. Runtime ABI 1.10 adds
`astra_rt_timer_create/set/cancel` and `astra_rt_cancel_wait` (Runtime.kit
1.10.0; `test-astra-image-providers.py` updated). Dist ships
`include/lua`, `include/ncursesw` (term.h, termcap.h, ncurses_dll.h) and
`docs/xml` (Doxygen XML).

**Phase 4 -- docs.** Strict gate passes in Docker (OrbStack on the Mac):
Doxygen 0 warnings, Sphinx HTML `-W -n` 0 warnings, PDF builds; 92 headers
documented, each on exactly one API page (`tests/test_documentation.py`
enforces it; group directives forbidden). `JAVADOC_AUTOBRIEF` on, so the XML
has briefs. Guides rewritten against current code: getting-started,
development, filesystem, graphics (window path), fonts (font.library),
message-ports (limits), resource-lifetime, interface-kit (real names),
README. New API pages: core, runtime, objects, applications, filesystem,
network, graphics, interface, events, pcm, midi, kernel-abi, tasks.
Factual errors fixed in comments (mutex_swap/mutex_lock swap, LIBRARY_SNAPSHOT
layout, stray LOG_WRITE essay, AstraHostCommand scope, TTY_FLUSH_OUTPUT no-op).

**Verification status.** Beast full verify (`~/astra-mg/verify-then-publish.sh`)
passed every gate on the tree as of phase 3 except userspace tests (an ntp
host-test include path, fixed; `make -C sw/userspace test` then UTEST=0).
The docs/header-comment edits after that have only run on the Mac
(`header-contract`, docs gate). **Re-run the full verify on the final tree
before committing.** `make -C ndk dist dist-check` has not been run since
the dist recipe changed (lua/ncursesw/xml) -- run it on beast.

## Phase 5 -- examples (done)

Every `ndk/examples/*.c` is a complete native program (`ASTRA_PROGRAM`,
`astra_main`, prints one line to `STDOUT`). `ndk/examples/Makefile` ships in
the archive and builds each from the installed kit, linking only the
libraries it calls (`<name>_LIBS`); `check_dynamic_executable.py` is given
exactly those as `--needed`. `make -C ndk dist-check` builds them into
`ndk/build/dist/examples`, and `emu/qemu/test-ndk-examples.py` installs each
as `/local/commands/ndk-<name>`, runs it from zsh and requires its exact line
and exit 0 (an example without an expectation fails the gate). New:
`hello.c` (the getting-started program) and `timer.c` (runtime 1.10 timer
wrappers). `text_clipboard.c` uses `astra/bytes.h`, not `<string.h>`.
The interface examples are driven headless through
`astra_interface_ui_handle_event` (Tab/Enter/Right, a wheel notch).

Consequences:
- Terminal commands are now granted `CLIPBOARD`, as they are `GUI` and
  `PCM` (`console_session.c`). `ASTRA_CAPABILITY_CLIPBOARD` moved to the
  public `clipboard.h`.
- `dist-check` deletes `build/dist/{smoke,examples}` first: the archive is
  dated 1970 (`--mtime=@0`), so an earlier product always looked current.
- The SDL smoke link listed `$(ASTRA_POSIX_LIBS)` before the SDL libraries;
  the NEEDED-order check failed. It had not run since the hard-float day.
- `make -C ndk example` is gone; `dist-check` is the build.
- beast `~/astra-mg/verify-nopc.sh` now runs `make -C ndk docs-check
  dist-check` (`NDKDIST=`) and the examples gate (`ndk-examples EXIT=`);
  previous copy `verify-nopc.sh.bak-20261004`.

## Not done

- **DE25 deploy of the posted cursor.** QEMU, helper and ROM change together
  (mailbox 1.8, kernel ABI `0x0001003C`): `emu/qemu/build.sh arty` on beast,
  then `emu/qemu/publish-de25-release.sh`. Measure with
  `/data/motion-ab.py 15 125` on the board: target ~21 Doom presents/s with
  the mouse moving (was 8.0).
- No runtime wrapper for IRQ/device revoke (driver-only, deliberately).

## Cursor (done in `34d137ef`; board measurement pending)

Symptom was Doom on the DE25 falling from 21.4 to 8.0 presents/s while the
mouse moved: every move was a full display request on the one-at-a-time
mailbox (~110 requests/s). Now `ASTRA_SYSCALL_DISPLAY_CURSOR` (104) writes
Vesta `DISPLAY_CURSOR` (0x738), a posted latest-value word; QEMU copies it
into mailbox 1.8 (`cursor`, `cursor_sequence`, futex on `wake_sequence`) and
the helper commits the newest word to the pointer plane, deferring while the
plane has not latched the previous one. The interim mechanisms are gone.
`test-display.py` checks it: ~1000 cursor updates cost 3 device requests.

## Also in `34d137ef`

- SDL Kit `SDL_config_minimal.h` defines `HAVE_STDLIB_H/STRING_H/MATH_H/
  CTYPE_H`; gate `sw/userspace/sdl2/tests/libc_prelude.c` in `target-check`.
- picolibc `sys/errno.h` includes `<errno.h>` (installed on beast via
  `mk/build-picolibc.sh`); recorded in `ASTRA_VENDOR.md`.
- `emu/qemu/bench-frame.py --pointer-hz` (pointer motion in screen pixels).

## Quake (parked)

Owner chose: GCC driver owns the dynamic link (default crt0-dynamic.o,
astra_user.ld, libc/runtime/compiler .library; -static keeps today's image),
and a CMake `astra_program()` function for program identity. Not started.

## Pointer vs frame rate (2026-10-05, uncommitted, on the board as 1519d8ac)

Measured on the DE25 with Doom and SDL Sprites (`/data/pointer-smooth.py
SECONDS HZ` drives QMP motion and samples FPGA pointer commits; `/data/
timeline.py` logs per second while a person rolls the trackball;
`/data/fd-astat.py` counts audio gaps; `/data/ctxsw.py`, `vcpuwait.py`,
`threadcpu.py` for host threads).

Changes in the tree (Haiku-modelled, see `~/Git/haiku` InputServer.cpp
fCursorBuffer and app_server EventStream / cursor loop):
- Input service publishes pointer motion into a shared seqlock area the
  display hands it at CONNECT (`ASTRA_INPUT_CONNECT_SHARED_POINTER`,
  `AstraInputPointerState`, sink-level in `input_port_sink.c`); the display
  samples it at vblank and before every input event (`sample_pointer`).
- QEMU input device coalesces unread motion per axis (never the head
  record) and interrupts for motion-only contents once a frame, half a
  frame after vblank (`astra_input_coalesce`, `astra_input_irq_due`,
  `motion_timer`; QOM `astra-input-coalesced`). Keys/buttons interrupt at
  once, behind the motion queued before them.
- Mailbox 1.9: helper publishes the panel's real scanout timing
  (`AstraDisplayScanout` at offset 64, from SCANOUT_LINES_BUILT; panel is
  60.08 Hz); QEMU's vblank follows it (`astra_next_vblank`). Without a
  helper it stays a 60 Hz timer.
- Helper has a cursor thread (`cursor_loop`, futex on cursor_sequence,
  `pointer_lock`); the request loop no longer handles cursor moves.
- `bench-frame.py --pointer-hz` fixed (`machine.qmp.point`), adds
  `--pointer-center`.

Results (QMP motion at 125 Hz, Doom): presents 21 -> 15.8 (was 14.9 before
all of this); cursor commits 25.8/s with 133 ms gaps -> 52.7/s, max 34 ms.
SDL Sprites holds 60.0 fps with motion, 0 audio gaps. Real trackball
(before the device coalescing): Doom 21.4 -> 18.3.

Open:
- Audio gaps with Doom under motion: media ran at 16, Doom's priority.
  Done (2026-10-04, below): priority is one number, decoupled from
  required/critical; media runs at 24. Board measurement pending.
- Doom loses ~25% under QMP motion; part is QMP-specific (monitor iothread,
  BQL contention: vCPU blocked on a mutex 5.4% of the time). Re-measure
  with the trackball on 1519d8ac.
- Cursor 52.7 commits/s under Doom (not 60): not yet explained.

## Scheduler priority: one number (2026-10-04)

Owner decisions: one priority per thread with named bands, not two settings;
users may raise their own processes to 19; manifest `priority=N`; no media
CPU budget yet; no automatic foreground boost; no stutter-driven boost
(reactive: the gap is already heard -- adapt buffer depth instead).

- Scale 1-27 (`sw/include/astra/process.h`): 1-7 background, 8-19
  applications (default 16), 20-23 system, 24-27 media. Kernel is not on the
  scale. Kernel ABI `0x0001003D`.
- `AstraLaunchArguments.priority` (now 24 bytes; exec request 36). Zero is
  16. A launcher may not exceed its own ceiling; the initial supervisor is
  exempt. Ceiling = max(19, launch priority).
- `ASTRA_LAUNCH_FLAG_ESSENTIAL` is memory protection only
  (`KernelProcess.essential`); it no longer sets 20.
- Supervisor: manifest `priority=N` on service lines; carried in
  `AstraServiceDefinition.priority` (service-manager protocol 6; persisted
  as `priority N`). ADD and `/config/services` files refuse above 19.
- Image manifest: every required/critical service `priority=20` (unchanged
  behaviour), media `priority=24`.
- Kernel bug found by the new test: PROCESS_CREATE dropped the arguments
  block unless count/environment/flags were set.
- Not done: a `nice`/`renice`-style command (POSIX setpriority works from
  inside a process); media CPU budget; DE25 measurement of audio gaps with
  Doom under motion (`/data/fd-astat.py`).
