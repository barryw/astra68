# Handover 2026-10-04: NDK cleanup (uncommitted), cursor, SDL/errno

Read `CLAUDE.md`, `AGENTS.md`, then this page. Nothing below is committed.
`git status` shows ~200 changed paths: all of it is this work.

## Owner decisions taken this round

- `ndk/` holds only public headers, docs, examples, make fragments and
  distribution tests. Implementation moved out (see below).
- Dead/stub API is deleted, not kept. Real code that no dynamic program could
  call is exported instead.
- Every symbol a shipped library exports is public API with an NDK header.
- Examples must be complete programs, linked from the dist archive and run in
  a QEMU gate (phase 5, NOT started).
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

## Not done

- **Phase 5: examples.** Make every `ndk/examples/*.c` a program with `main`
  or `astra_main`, build from the extracted dist archive, bundle, and run in
  a QEMU gate that checks output; include the getting-started `hello` and a
  timer example (exercises the new runtime 1.10 timer wrappers). Fix
  `text_clipboard.c` (needs hosted flags). Docs literalinclude the same files.
- Add `make -C ndk docs-check` to the beast verify (needs Docker there).
- Remaining audit items: no runtime wrapper for IRQ/device revoke (driver-only,
  deliberately skipped).

## Cursor (in progress at handover)

Symptom: moving the mouse cut Doom on the DE25 from 21.4 to 8.0 presents/s
(`/data/motion-ab.py 15 125` on the board; `/data/disp-ab.py` shows device
counters). Cause: every cursor move was a full display-device request on the
one-at-a-time mailbox (interrupt + completion round trip), and the device
completes only ~110 requests/s. Interim changes in the tree (vblank pacing,
helper `pointer_move` deferral, cursor riding on render-only batches, idle
heuristics in `services/display/main.c`) got it to 13.7 only. Decision:
replace them with a dedicated posted cursor channel, as KMS/DWM/WindowServer
do -- a latest-value cursor register outside the request queue, no
completion -- and delete the interim mechanisms. See the cursor section of
the next handover or the commit that lands it.

## Also uncommitted from earlier this session (verified on beast)

- SDL Kit `SDL_config_minimal.h` defines `HAVE_STDLIB_H/STRING_H/MATH_H/
  CTYPE_H`; gate `sw/userspace/sdl2/tests/libc_prelude.c` in `target-check`.
- picolibc `sys/errno.h` includes `<errno.h>` (installed on beast via
  `mk/build-picolibc.sh`); recorded in `ASTRA_VENDOR.md`.
- `emu/qemu/bench-frame.py --pointer-hz` (pointer motion in screen pixels).

## Quake (parked)

Owner chose: GCC driver owns the dynamic link (default crt0-dynamic.o,
astra_user.ld, libc/runtime/compiler .library; -static keeps today's image),
and a CMake `astra_program()` function for program identity. Not started.
