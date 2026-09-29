# Handover 2026-09-26: SDL2 on the graphics hardware — ready for DE25 testing

Read `CLAUDE.md`, `AGENTS.md`, then this page. The authoritative contracts
are `docs/MANAGED_GRAPHICS.md` (software path) and `docs/TEXTURE_ENGINE.md`
(hardware); `docs/CURRENT_STATE.md` carries the same status in brief.

## Where things stand

**Everything is uncommitted** (~170 changed/new files: Codex's tile removal
from 2026-09-25 plus this session's work). Nothing new has run on real
hardware. Next session's job is physical validation on the DE25, then commit.

Goal (user): every SDL2 2D renderer feature executes on the FPGA; no
MC68040 pixel work; unchanged SDL2 apps perform well.

### Done and verified off-board

| Area | State | Evidence |
|---|---|---|
| Tile layers removed (Codex, 2026-09-25) | RTL, MMIO, Copper, NDK, tests; framebuffer scroll/wrap and sprites kept | full graphics sim suite; DE25 route 41,077 ALMs; **deployed** to the board (see Board below) |
| Managed graphics (`graphics.h` real) | window-scoped GPU surfaces in Media RAM, ADLT v1.4 draw lists, render-only batches (v1.4), staging writes, readback | host tests at every layer, each perturbation-checked |
| SDL2 `astra` renderer + surface-window video backend | fills, points, lines, copies, CopyEx any angle, `SDL_RenderGeometry`, color mod, NONE/BLEND/ADD/MOD/MUL, linear filter, ARGB8888 targets, `RenderReadPixels` | `test_astrarender`, `test_astravideo`; QEMU gates |
| Texture engine RTL (TRIANGLES op, ARGB8888 destinations for all ops) | matches `sw/userspace/graphics/src/texture_reference.c` bit for bit | `fpga/arty/graphics/run_tests.sh` (194 generated triangle cases + directed/stress/rejection), perturbations caught |
| DE25 production route of the texture engine | closes every clock | 45,925/46,800 ALMs, 304/358 RAM, 53/376 DSP; setup +0.051 ns HDMI, +0.364 ns 165 MHz; entry in `fpga/de25/TIMING_CLOSURE.md` |
| Display mailbox overrun | fixed: payload now holds a full 8 MiB batch (was 4,147,200 B) | builds; gates |
| `test-display.py` (failed on HEAD d7fed3bb too) | root cause: every window change re-presented the cursor; now only on shape change | gate passes |
| system.library | 2.4 (`ASTRA_SYSTEM_2.4`); Runtime.kit provides 2.4.0, SDL.kit requires it | `system-library-contract` |

QEMU gates last run green on beast (fresh ROM/userspace/QEMU/images):
`test-sdl-video.py`, `test-sdl-draw2.py`, `test-sdl-render.py` (new: 4
triangle commands, 3 surface reads), `test-display.py`, `test-terminal.py`.
QEMU counts commands but **does not render pixels** — no pixel has been
verified anywhere yet.

### Not verified / known gaps

1. No real pixels from TRIANGLES, ARGB targets, or blended paths.
2. Readback (`ASTRA_DISPLAY_FRAME_READ_SURFACE`): helper arena→mailbox copy
   and QEMU mailbox→guest copy-back only ran in self-tests.
3. ~~No test feeds `render_builder`'s TRIANGLES output into the model.~~
   **Closed:** `test_session_triangles_on_reference_model`
   (`sw/userspace/graphics/tests/test_surface.c`) decodes built batches per
   §1 and runs them through `texture_reference.c` — 1:1 bilinear copy and
   FLIP_X exact per texel, 2x nearest upscale with modulation and clip,
   past-range quad cut to clip, ADD fill, client triangle fan with clip.
   Perturbing the model's pixel-centre or clip-right rule fails it.
4. Blit alpha semantics changed: `FLAG_BLIT_ALPHA` is now straight-alpha
   SDL BLEND (was premultiplied). Existing desktop/UI output using alpha
   blits may change slightly — look for it on HDMI.
5. Blitter converts to RGB565 by truncation, triangles round: a copy and a
   triangle of the same texture can differ by one LSB.
6. Performance (sim, zero-latency memory, 165 MHz): untextured 8.7 Mpx/s,
   nearest 3.5, bilinear 2.2; ~700 cycles setup per textured triangle.
   Plain/alpha copies stay on the fast blitter. Needs DE25 measurement.
7. Streaming-texture uploads still cost one MC68040 copy in the display
   service (DMA-backed staging deferred; `MANAGED_GRAPHICS.md` §3).
8. Rejected on purpose: INDEX8 textures on triangles, text onto ARGB8888
   targets, ADLT circle/ellipse/pattern/flood/text-layout via `graphics.h`.
9. FPGA capacity: **875 ALMs free**; all 3-bit AXI read IDs used. The next
   FPGA feature must reclaim logic first (largest candidate: share the
   texture engine's six 50-bit attribute steppers).
10. Fullscreen / logical-resolution scaling for SDL (Doom full screen) is
    still `MANAGED_GRAPHICS.md` phase 5 — not started.

## Board (astra-de25, 192.168.1.52, via beast)

- Running: tile-free FPGA image (2026-09-25 route) + runtime release
  `2006eb8d…` (Codex's deploy). POST PASS, stage 8, services active. HDMI goes
  to the TV; the Mac's Cam Link is **not** attached (captures show NO SIGNAL).
- Rollback material on the board: FPGA boot files in
  `/var/lib/astra/boot-rollback-01d05c63`; previous runtime release is the
  `previous` symlink in `/var/lib/astra`.
- The board does **not** yet have the texture-engine FPGA image or any of
  this session's software.

## Next session: physical test plan

Ask the user before touching the board. Use `rtk`-prefixed commands per
`AGENTS.md`; source syncs with the checksum rsync; retain session IDs of long
remote jobs.

1. ~~Close gap 3~~ — done.
2. **Build matching artifacts on beast** from the exact current source:
   - FPGA: routed shell already at
     `build/de25-texture-engine/astra-shell` (verify `BUILD_SHA256SUMS`;
     rebuild with `fpga/de25/build_astra_shell.sh` if source changed since).
     Boot bundle: `fpga/de25/build_boot_bundle.sh <shell>` →
     `build/de25/boot`.
   - Runtime: `emu/qemu/publish-de25-release.sh` (QEMU `arty` cross build,
     ROM, storage image, helper, audio host, remote desktop), then
     `python3 tools/astra_release.py verify <release>`.
3. **Install**: copy the boot bundle to the board, `sha256sum -c`, run
   `install_boot_bundle.sh /dev/mmcblk0p1 /mnt/astra-boot`, reboot, check
   `fpga/de25/verify_running_shell.sh <shell>` on beast, then
   `emu/qemu/deploy-de25-release.sh <release>`. The old runtime will fail
   against a new FPGA version until the new release is active — expected.
4. **Hardware certification**: run the ARM `astra_render_certify` build from
   `build/de25-texture-engine/linux/` on the board (ASTRA_TEXTURE phase:
   7 triangle cases + rejection, ARGB FILL/LINE/BLIT/BLIT-alpha, all against
   the reference model) plus the existing phases. Stop Astra first if the tool
   needs exclusive graphics access — check its usage text.
5. **System checks**: POST, stage 8, services with zero restarts; HDMI desktop
   on the TV (ask the user to look, or attach the Cam Link to the Mac:
   `ffmpeg -f avfoundation -framerate 30 -video_size 1920x1080 -pixel_format
   bgr0 -i "0:none" -vf "select=gte(n\,45)" -frames:v 1 out.png`); Terminal
   works; desktop alpha/rounded corners look unchanged (gap 4).
6. **SDL on hardware**: launch SDLVideoProbe, SDLTestDraw2 (upstream
   testdraw2), SDLRenderProbe (new: triangles, rotation, modulation, blend
   modes, ARGB target, readback) on the board; check pixels on HDMI and the
   probe's own readback assertions; measure frame time.
7. Record results in `fpga/de25/TIMING_CLOSURE.md` (hardware qualification)
   and `docs/CURRENT_STATE.md`; then commit in logical chunks (tile removal;
   managed graphics + SDL renderer; texture engine RTL; readback) with all
   gates green.

If the board misbehaves: roll back FPGA (`boot-rollback-01d05c63`) and
runtime (`previous`) together — they are version-coupled.

## Where the work lives

- Hardware: `fpga/arty/graphics/astra_render_texture.sv` (new),
  `astra_render_command_processor.sv`, `astra_render_blitter.sv`,
  `astra_render_glyph.sv`, `sim/tb_astra_render_texture.sv`,
  `sim/texture_vectors.c`; protocol JSON
  `fpga/arty/graphics/protocol/astra_render_v1.json` (TRIANGLES = 768).
- Model: `sw/userspace/graphics/src/texture_reference.c`,
  `include/astra/texture_reference.h`, `tests/test_texture_reference.c`.
- Lowering: `sw/userspace/graphics/src/render_builder.c`;
  wire format `sw/include/astra/draw_list.h` (ADLT v1.4).
- Display service: `sw/userspace/services/display/window_graphics.{c,h}`,
  `main.c` (graphics dispatch, render-only/readback submission, cursor fix).
- Transport: `sw/include/astra/{render_batch,display,display_mailbox}.h`,
  kernel `sw/kernel/{process,platform}.c`, QEMU
  `emu/qemu/qemu-9.2/hw/m68k/astra68.c`, helper
  `fpga/arty/linux/astra_terminal_display.c`.
- NDK: `ndk/src/graphics.c`, `ndk/include/astra/{graphics,window}.h`,
  `ndk/system.library.exports`.
- SDL: `sw/userspace/sdl2/render/SDL_astrarender.c`,
  `video/SDL_astravideo.{c,h}`, `prepare_source.py` (overlay registration),
  probes under `sw/userspace/sdl2/tests/`, gate
  `emu/qemu/test-sdl-render.py`.
- Beast helpers (throwaway, not in git): `~/astra-mg/{build-all,images,gates}.sh`
  rebuild everything, make images (`astra_image.py --test-app`), and run the
  five QEMU gates. Latest QEMU binary under
  `~/.cache/astra68/qemu-9.2.4/build-host-*/qemu-system-m68k`.

## Session 2 (2026-09-26) — hardware results, then applications done properly

### Hardware (done)
- Texture-engine FPGA installed on astra-de25 (`build/de25-texture-engine`),
  `verify_running_shell` PASS. Matched rollback: FPGA
  `/var/lib/astra/boot-rollback-tilefree-2006eb8d` + runtime `2006eb8d…`
  (FPGA `boot-rollback-01d05c63` pairs only with the older runtime).
- `astra-render-certify`: every phase PASS on hardware incl. `ASTRA_TEXTURE`.
  The first case-5 failure was a certify bug (host source slots sized
  without row padding, 572 vs 616 bytes); RTL was correct, proven by
  replaying the exact case in sim at hardware addresses and reading DDR back.
  Certify now verifies fixtures before submit/after run and reports all
  mismatches.
- Runtime release `6e7a06d6…` deployed and running (POST, stage 8, services).
  Probes passed on hardware but only via `--test-app` images, which stop the
  real system — the user rejected that: apps must run from the live desktop.

### Phase 1 of docs/APPLICATIONS_SDL_SPRITES_PLAN.md (implemented, host-tested)
Launch ceiling policy (supervisor + image builder), application catalog
(filesystem.library 4.1), desktop icon grid on a GPU-surface window
(icons = ARGB surfaces, labels = `astra_draw_ui_text`), reusable
`tools/astra-bundle.mk` + `.apps` inventories, BMP icons in `aicon.py`,
string library `astra/string.h` (runtime 1.9) replacing 11 private copies,
system.library 2.5 (`astra_draw_ui_text`, `astra_aicon_strike_argb`).
Gates now locate Terminal from the desktop's `desktop icon <bundle> L T W H`
log line (`Machine.desktop_icon`). The userspace boundary test was already
failing at HEAD (desktop Makefile `all:`); fixed, plus two older violations.

### Verified on beast (phase 1)
Clean full build, every library contract (runtime 1.9, system 2.5,
filesystem 4.1), NDK/tools/userspace host suites incl. boundaries, FPGA sim
suite, and QEMU gates: terminal (99 commands), display (reworked: waits for
a quiet display instead of exact first-frame counts, clicks the logged
Terminal icon via the shared `TraceReader` in test-terminal.py), sdl-video,
sdl-draw2, sdl-render, and the new `emu/qemu/test-desktop-apps.py` (opens
Interface Gallery and TestDraw2 from the live desktop, all services up).

### Remaining, in order
1. ~~Bug B~~ fixed and verified in QEMU and on the DE25 (session 3). Board runs release `8552e574…`.
2. Phase 2: SDL `filesystem/astra` (base `/app/`, pref `STORE`), RWops on
   `astra_filesystem_*` (find how a POSIX program's process filesystem is
   reached), `testutils.c`, ship testgeometry/testsprite2 as apps.
3. Release, deploy, launch from the desktop on the DE25, then show the user.
4. Phases 3-4 (sprites) per the plan.
5. Nothing is committed; commit only when the user asks.

### Bug B: dead display service never restarted -- FIXED, verified in QEMU (session 3)

Session 3 found two more faults behind the re-grant fix, each only reachable
once the one before it was fixed:
- **Input seat stays with the dead display.** `services/input/main.c`
  noticed a dead client only when it next delivered an event, so the
  restarted display's seat-owner connect was refused BUSY (display exit 11).
  `detach_dead_clients()` now probes every client port
  (`astra_wait_one(h, 0u, NULL)` -> PEER_DEAD/CLOSED) before judging a connect.
- **IRQ endpoints keep the dead owner's state.** PID 1 owns them; the
  successor's `astra_irq_arm` got INVALID_STATE (display exit 32,
  DISPLAY_FAIL_ARM). The supervisor's `reset_exclusive_devices()` now calls
  `quiesce_endpoint()` first: mask, then read+ack stale records. With
  ADMIN_MASKED set, retiring the last record leaves the endpoint MASKED (no
  re-arm race). A sticky OVERFLOW/STORM/DEVICE_ERROR flag would need
  IRQ_RECOVER, which the runtime does not wrap -- logged as
  `service restart irq failed NAME`, marked `ponytail:` in loader.c.
- The supervisor is in the ROM's initial user image: rebuild `sw/boot`, not
  just `sw/userspace`, or you are testing the old PID 1.

Verified on beast: `test-desktop-apps.py` PASS incl. `restart_display_and_recover`;
five gates, all six terminal power modes (shutdown/restart, veto, menu),
`test-astra-image-providers.py`, `make -C sw/userspace test` green.
Perturbations each fail the gate: no IRQ quiesce (exit 32 loop), no input
probe (`service restart:11`), original close loop restored (`service restart:8`).
**On hardware (release `8552e574…`, deployed session 3):** POST PASS,
stage 8, services active, 0 restarts. Live desktop: opened Terminal from its
icon (72,224), typed `service restart display` over QMP: `service restart
reset DISPLAY`, display relaunched status 0, desktop relaunched and re-logged
its icons, pointer reaches (300,300) and (1200,600), 52/52 submissions
completed. Board probe helpers: `beast:~/astra-mg/hw/{board_qmp.py,join.py}`
(copy on the board in `/data/cert/`); stopping `astra-remote-desktop` makes
the guest remote-desktop service retry with status 16 until it is started
again -- expected, it recovers.
Open: when the display restarts under an open Terminal (reproduced on the
board, and in QEMU before the IRQ fix), the Terminal's zsh logs `zsh: crashed: pc 0x00000000 address 0x00000000
vector 0`, exit 126; the desktop also exits 12 before being relaunched. A
shell must not jump to NULL because its console's display went away --
investigate next.

Original session-2 diagnosis:
Root cause: `sw/userspace/supervisor/src/loader.c` (end of
`supervisor_loader_start`) closed PID 1's DISPLAY/INPUT device and IRQ
handles after boot, contradicting the comment above it. Every restart then
duplicated a dead handle into the new process: `astra_launch_executable_stream`
status 3 (`ASTRA_SYSCALL_INVALID_HANDLE`), mapped to `ASTRA_STATUS_INVALID`
= 8 (`service restart:8`), retried forever by `retry_failed_services`. The
desktop (a non-resident `application`) exited with the display and was never
relaunched. Evidence: `beast:~/astra-mg/hw/freeze.txt` seq 1561-1690.

Fix written (host tests pass; NOT yet built for m68k, gated, or deployed):
- loader.c: the close loop is removed; PID 1 retains the handles.
- loader.c: `reset_exclusive_devices()` (called from `launch_definition`,
  the restart/start path) resets every device a restarting service is the
  sole startup-manifest grantee of (`manifest_grantees() == 1`); shared
  devices (HOST_DEVICE) are never reset. Logs `service restart reset NAME`.
  Needed because retained handles mean the kernel no longer auto-resets the
  device when the service exits.
- `emu/qemu/astra_image.py`: desktop line is now `service /services/desktop`
  (resident, restart on fault); `test-astra-image-providers.py` updated.
- `emu/qemu/test-desktop-apps.py`: new `restart_display_and_recover` step
  (open Terminal, `service restart display`, require the desktop to log its
  icons again and the pointer to work).

Next session, in order:
1. Sync to beast, `make -j32 -C sw/userspace all`, `make -C sw/userspace test`.
2. Rebuild images (`~/astra-mg/images.sh`), run all gates
   (`~/astra-mg/gates.sh` + `test-desktop-apps.py`). Check the restart step
   really exercises the re-grant: perturb by restoring the close loop and
   confirm the gate fails.
3. Check that a resident desktop does not break shutdown/restart actions or
   the `required` semantics (it is not `required`).
4. Release (`emu/qemu/publish-de25-release.sh` with
   `ASTRA_DE25_RELEASE_OUTPUT`), verify, deploy, confirm on the board.

### Fixed bug A: pointer move after closing a window killed the display
`render()` treated "nothing damaged" as INVALID; the input path called it
unguarded when hover state changed on an already-closed window. Now both
callers use `present_changes()` (display main.c): frame if damaged, else
cursor only, else nothing. Seen only on hardware; the QEMU gate
`test-desktop-apps.py` replays the user's drag + close-gadget sequence but
did not reproduce the timing.

### Board state at end of session 2
Release `c5670ab3…` (phase 1 + fix A) running; POST PASS, stage 8, all three
services active. Rollback: previous release via `previous` symlink; FPGA
rollback `/var/lib/astra/boot-rollback-tilefree-2006eb8d` (texture-engine FPGA
is current and certified). Scratch helpers on the board: `/data/cert/`
(`probe_qmp.py`, `probe_run.sh`, certify binary); on beast `~/astra-mg/`.
Probing a live board: stop only `astra-remote-desktop`, use
`/run/astra/remote-desktop-qmp.sock` for `pmemsave` of the trace ring
(0x020C4000, 64 KiB), decode with `~/astra-mg/hw/decode_ring.py`, restart it.

### After bug B
- Phase 2 (SDL platform layer + upstream color demos) per
  `docs/APPLICATIONS_SDL_SPRITES_PLAN.md` §5.
- §6b (user request): every app gets a menu (default "About {App Name}"
  dialog, "Quit {App Name}" on Alt-Q), window title = app name from one
  declaration (bundle manifest `name`), and standard project templates
  (application, service, command, kit). Design and confirm defaults with the
  user before building.
- Phases 3-4: hardware sprites (window-bound mode is occluded via the scene
  mask, per the user's decision).
- Nothing is committed. Commit only when the user asks.

## Session 3 (continued) -- service tiers and user service policy

User decisions: system services are untouchable and always running; storage
and display death is fatal (until a text mode exists); users control their
own services, including "start at boot" and restart policy; a required
service that keeps failing backs off forever (no escalation).

Built (contract: `docs/USERSPACE_ARCHITECTURE.md` §5; summary in
`docs/CURRENT_STATE.md` "Service tiers and user service policy"):
- Manifest words `critical`, `required`, `start=boot|manual`,
  `restart=never|on-fault|always` (`loader_manifest.c`); storage and display
  are `critical`, desktop is now `required` (`astra_image.py`).
- Kernel `ASTRA_SYSCALL_SYSTEM_FAIL` (100), PID 1 only, bounded printable
  message, panics with it (`process.c`, `kernel.c`, host test).
- Supervisor: every service-manager operation on a PROTECTED service -> ACCESS;
  `ASTRA_SERVICE_CRITICAL` flag; critical death or failed critical boot launch
  -> `system_fail("critical service NAME exited: status N")`; per-service
  restart backoff (immediate, 0.2 s doubling, 10 s cap, reset after 60 s up);
  `/config/services/NAME/policy.conf` overrides for manifest services, obeyed
  at boot, in list/inspect and in restarts; atomic `service_file_write`.
- Service-manager protocol v5: request carries `value`; new
  `ASTRA_SERVICE_MANAGER_SET_RESTART`; NDK `astra_service_set_restart`
  (added to the unreleased `ASTRA_SYSTEM_2.5` node).
- `service` CLI: `list` shows TIER/BOOT/RESTART; `inspect` shows `tier:` and
  `boot:`; `set NAME restart POLICY`; "not permitted: the system manages this
  service". `enable` on an added service also sets start=boot.
- `fault-probe` fixture service + `astra_image.py --fault-probe` /
  `--fault-critical`; gate `emu/qemu/test-service-policy.py` (default and
  `--critical`). `test-desktop-apps.py` lost its display-restart step (now
  refused by design); `test-remote-desktop-service.py` reads `boot:`.

Verified on beast: kernel, NDK, supervisor and userspace host tests; both
policy gates PASS (tiers refused; 8 probe deaths backed off; media
disable/restart=never persisted across a clean shutdown and was obeyed at
boot; fault-probe as critical halts naming it). Perturbations each fail their
gate: no ACCESS refusal, no backoff (26 deaths in 6 s), override not applied
at boot, no system_fail on critical death.

Gap: no gate kills a service that holds an exclusive device any more (display
was that path). The re-grant/reset/IRQ-drain code is proven by the earlier
perturbations and the DE25 run; a future fixture could hold a spare device.
Released to the DE25 (see below).

### DE25 end-to-end (release `2fba6626…`) and the storage hang it found

Driver: `beast:~/astra-mg/hw/board_e2e.py SOCKET before|after`, run on beast
with the board's `/run/astra/qmp.sock` forwarded over ssh
(`ssh -N -L /tmp/de25-qmp.sock:/run/astra/qmp.sock root@192.168.1.52`); it
reads the trace ring with QMP `xp` and reuses the gates' Qmp/Shell code.
Passed on hardware: every service launched status=0, pointer, Terminal from
its icon, all tiers listed right, nine refusals, remote-desktop
stop/start/pause/resume/restart (new PID), media disable + restart=never,
guest `restart` (QEMU exit 89 -> Linux reboot -> Astra back unattended),
media not launched at boot and choice persisted, started by hand and
restored, Interface Gallery + TestDraw2 opened from the live desktop,
display keeping up (34 frames/2 s with TestDraw2 on the DE25), backoff with
the host helper stopped (8 failed relaunches in 25 s, recovered within 15 s).

The first phase-1 run hung the machine (launches and input dead, CPU idle).
RAM dump analysis (`beast:~/astra-mg/hang/`, 128 MiB pmemsave + scripts):
a storage worker held the ext4 mount lock waiting forever for a FLUSH whose
completion the kernel had already recorded (slot COMPLETED, block
completed 0x3a5 vs IRQ delivered 0x3a4); every other storage thread, the
supervisor (VFS open), zsh's exec and the desktop chained behind it.
Root cause, two pre-existing races that drain a completion without any
wakeup for its waiter:
- kernel maintenance ran `kernel_block_service` on every process exit and
  could take a live owner's completion out of the ring before its interrupt
  was captured -- fixed: maintenance drains only when a dead owner's request
  is REVOKING (`kernel_block_revocations_pending`, test in test_block.c);
- storage published a lane as active only after submitting, so a sibling's
  collect in that gap drained its completion unpublished -- fixed:
  `catch_up()` in `sw/userspace/storage/src/lease_block.c` collects once,
  serialized, after the lane is visible (host test
  `test_completion_drained_before_the_lane_waits`, perturbation-checked).
Not reproduced deliberately: ~250 churn commands on the old build ran clean,
so the fix rests on the dump's kernel counters and the modelled states.

Also fixed: the supervisor's retry pass could loop forever on a launch that
fails early (it now pushes each record past its backoff before trying);
`test-filesystem-stress.py` counted only 15 of the emulator's 23 host-FS
operations and now stops the two user services that use the host channel
before it measures (accounting exact: 12,471 commands).

## Display performance (session 3, in progress)

Target (user): 60 fps with ten TestDraw2 windows; the MC68040 essentially idle;
hardware must never be the bottleneck.

Measured on the DE25 (release `25727bf9…`, TestDraw2 open):
- Before: 17 display batches/s; Linux helper `astra-terminal-display` 56% of
  its core, 89.5% of that in `astra_window_scene_compile` (whole scene
  recompiled per compose, byte stores into the uncached /dev/mem arena, every
  layer re-decoded per line); arena mmap/munmap per batch; every hardware wait
  polled with 1 ms nanosleep. Guest display 6.7%, TestDraw2 7.4% CPU.
- Fixed in `fpga/arty/linux` (host tests pass; running on the board as
  `/data/cert/astra-terminal-display.fast` through `systemctl
  set-environment ASTRA_TERMINAL_DISPLAY=...`, not yet in a release):
  compile into cached memory with layers decoded once, published body then
  header; the arena mapped once per device (`astra_graphics_hw.c`, views);
  all six hardware polls 50 us.
- After: 47 batches/s, helper 33%. Now the FPGA render engine is the
  bottleneck: a TestDraw2 frame (512 commands) takes 7-10 ms of hardware
  time (~3000 cycles per small command at 165 MHz); a compose (15 commands,
  mostly full-window content->cache blits) 7.5 ms, then 4.9 ms to vblank.
  Sim figures agree: 8.7 Mpx/s untextured, 3.5 nearest, 2.2 bilinear.
Next: RTL analysis of per-command overhead and pixel throughput in
`fpga/arty/graphics` (command processor, blitter, AXI read path); remove the
per-frame content->cache copy in the display service; batch SDL points/lines.

