# Astra 68 — Handover: launch latency, and the window that costs a third of it

Date: 2026-08-19. Written to be read cold. Read `CLAUDE.md` first.

**The goal:** Terminal (or any application) launching in under 100 ms first
time, under 50 ms second time.

**Where it stands:** 3,283 ms → **967 ms** → **~925 ms** first launch. Not met.
Window creation was 335 ms and is now **293 ms**; the compositor work inside it
is roughly halved. What is left is not software — it is the blitter, §3.

Everything below was measured on `astra-arty`, not estimated.

## 2026-08-20 continuation: the filesystem floor is now systemic

The `fsopen` item and cross-process library work in §7 are implemented at the
shared boundaries, not in `ls`:

- the image builder writes validated, exact provider records under
  `LIBS:.providers/`, with the old manifest sweep as compatibility fallback;
- Axiom syscall 56 attaches an already-resident exact/compatible library
  identity, sharing read-only pages and privately copying cached initial
  writable pages;
- `STOR` v7 reads small provider records inline, without an area bind;
- `STOR` v8 carries a client's first path operation in `HELLO`, saving one
  service round trip for every short-lived filesystem client; and
- the shared Terminal launcher reads every command image through the existing
  one-request supervisor VFS path before `PROCESS_CREATE`.

Real-board progression for warm `filesystem.library` open was 794 ms on the
stock path, 149 ms after resident identity attachment, and 61 ms in two
consecutive v8 samples. Those two no-output `ls -l` listings were 86 and 51 ms,
down from the prior 174--258 ms range. The common launcher image stage measured
39 ms warm (92 ms before the fused VFS build). `which status` and `mkdir`
completed repeatedly through the same code; scheduler and terminal-output
variance remains large, so their end-to-end times are not used as a filesystem
microbenchmark.

The runnable protocol check is
`sw/userspace/vfs/tests/test_vfs_port.c:test_first_operation_shares_the_hello_round_trip`:
after lazy connect, the first `OPEN` has both a live session and a result after
one service request. No `ls` instrumentation or command-specific cache remains.

Sections below are the historical pre-fix measurements and failed experiments;
their ranked `fsopen`/library next steps are superseded by this continuation.

The exact clean candidate was ROM
`b5fabd384b1b5a8ab82aed8d064b22da0ea32b30a12cc94412a045898b39049a`
and pre-boot storage image
`ff7540bebc34bccd7a82a93426e1fd381c1359ea05dd3f56456b00fc673618f3`.
It passed POST, stage 8, and a final `ls`/`which`/`mkdir`/`rm`/missing-`cat`
sweep on `astra-arty` with no panic. The board now runs the untouched stock
`/data/astra/rom/astra_boot.bin` and `/data/astra/storage-terminal.img`, which
were restarted and independently passed POST and stage 8.

---

## 1. The launch, stage by stage

Terminal, first launch, current build:

| Stage | before | now | What it is |
|---|---:|---:|---|
| `window` create | 335 | **293** | compositor recompose — §3 |
| `fsopen` | 250 | 252 | VFS client connect + first `OpenLibrary` |
| `gfxkit` | 109 | 110 | graphics.library + font.library images |
| supervisor read+spawn | 88 | 90 | image off the volume, then `PROCESS_CREATE` |
| `icon` | 58 | 60 | app manifest + `.aicon` |
| `surface` | 17 | 18 | draw-list area |
| Terminal `ready` | 793 | **752** | |

Measured A/B in one session on `astra-arty`, same ROM and QEMU, the two storage
images differing only by the compositor build. The desktop's own boot-to-ready
went 1,001 ms → 947 ms over the same pair.

Second launch is ~789 ms; `fsopen` is **the same 250 ms both times**, which is
the single most important fact in this document: it is not data and not disk,
so caching does not touch it. It is round trips.

## 2. The two floors everything else sits on

**A cross-process round trip costs ~7.5 ms**, warm cache, trivial work. Measured
by timing 50 `astra_vfs_stat` calls on a path that does not exist. A same-thread
port round trip is 0.19 ms, so essentially all of it is the address-space
switch. A launch makes roughly thirty of them, so ~225 ms of the 967 is round
trips and no amount of filesystem work removes it.

The switch is expensive because the 68030 has no address-space tag: loading CRP
invalidates the ATC, and the emulator models that by flushing every user
translation. See §5 for the fix and why the first attempt at it failed.

**A device transfer costs ~6–7 ms fixed**, independent of size. So the unit of
I/O wants to be large, which is what `ASTRA_VFS_BULK_MAX` is now for.

## 3. The renderer is bandwidth-bound, and blit costs 5x what fill costs

This is settled now, on hardware, not modelled. The completion record each
render command writes back already carries a **pixel count** (word 3) and
**start/end hardware cycles** (words 4 and 5) — `astra_render_protocol.h`. The
Arty driver prints them per command under `ASTRA_DISPLAY_PROFILE_COMMANDS=1`
(`fpga/arty/linux/astra_terminal_display.c`, in `execute_render_batch`).

Two frames with the **same command count** cost 1.1 ms and 72 ms. Command count
is irrelevant; pixels are everything. The engine runs at 187.5 MHz and the
cycle counts sum to the measured `hardware_us` exactly.

| operation | cycles/pixel |
|---|---:|
| `FILL` | **3.0** |
| `BLIT` | **15–18** |

**Why blit is worse than fill.** `astra_render_blitter.sv:625` is
`assign m_axi_arlen = 8'd0;` — every source read is a **single-beat burst**,
and the state machine blocks on it: `ST_SOURCE_REQUEST` asserts `arvalid`,
waits `arready`, `ST_SOURCE_RESPONSE` waits `rvalid`, nothing else in flight.
A one-beat `source_cache` means 4 RGB565 pixels share a beat, so one pixel in
four pays a full DDR round trip: 15-18 cyc/px x 4 = 60-72 cycles a beat, about
350 ns — textbook non-pipelined AXI read latency to Zynq DDR. Fill never reads;
its writes are posted and merged 4 px per 64-bit beat.

**So the remaining renderer work is RTL, not software:** give the blitter a
burst read (`arlen` = a source row) and prefetch ahead of the pixel loop. That
takes blit from 18 cyc/px toward fill's 3, and it is the single largest lever
left anywhere in this document — the one blit a window drag genuinely needs is
32 ms today and would be about 6 ms.

## 3a. What the software side removed

Every compositor pass that painted pixels nothing would ever see. All of it is
application-generic.

| Was | Now |
|---|---|
| Opening a window filled the client area, then replayed a draw list whose first command filled the same area — two identical full-surface fills. | `astra_draw_list_covers` (`render_builder.c`) reports when a list opens with a full-surface fill; the compositor skips its own. |
| An undecorated window (`FULLSCREEN`, `DESKTOP`) was copied content -> cache -> framebuffer, and its cache was a byte-identical copy of its content. | `decorated()` in `compose`: undecorated windows compose straight out of content. One full-screen blit gone per frame. |
| Every layer painted across the whole damage rect, then higher windows painted over it. A drag repainted 422,592 desktop pixels and then covered 403,440 of them. | `rect_subtract` / `visible_region` cut every opaque window above a layer out of that layer's region. The same drag now repaints 9,200 desktop pixels. |
| `desktop_damage` filled canvas across the whole damage, then painted the system bars over their own rows. | The canvas fill is clipped to the work area. |
| Focus moving off the desktop window flipped its `ACTIVE` flag, dirtied its cache and damaged its full bounds — but a `DESKTOP` window draws identically either way. | `activate` only repaints decorated windows. |
| `astra_render_builder_rounded` drew a cross: a full-height centre band and a **full-width** middle band, repainting their intersection. | Side bands instead of a full-width one. For an 844x492 frame that is 383,760 pixels not painted twice. |

Measured on `astra-arty`, same session:

| frame | before | after |
|---|---:|---:|
| desktop first compose | 210.6 ms | **100.7 ms** |
| window-open compose | 181.2 ms | **124.7 ms** |
| **window drag** | **79 ms (12.6 fps)** | **41 ms (24 fps)** |

A drag frame is now 32 ms of the moved window's own blit — required work at
15 cyc/px — and 8 ms of everything else. Software has little left to give here;
see §3.

Turn the numbers on with `ASTRA_DISPLAY_PROFILE=1` for per-frame totals and
`ASTRA_DISPLAY_PROFILE_COMMANDS=1` for the per-command pixel/cycle dump:

```sh
ASTRA_DISPLAY_PROFILE=1 ASTRA_DISPLAY_PROFILE_COMMANDS=1 QEMU=... ROM=... \
  STORAGE=... setsid /data/astra/bin/astra-terminal-start \
  </dev/null >/data/astra/log/prof.log 2>&1
```

Terminal is not autostarted. `/data/astra/run/drive.py launch` double-clicks
its icon over QMP and `drive.py drag` drags the title bar, so both paths can be
profiled without touching the machine.

## 4. What landed, and where

All of it is application-generic — none of it is a Terminal special case.

| Where | Change |
|---|---|
| `sw/include/astra/vfs_service.h` | `ASTRA_VFS_OP_READ_PATH` (op 14, protocol v5): open+read+close as **one** round trip. `ASTRA_VFS_BULK_MAX` 16 KiB → 128 KiB. |
| `sw/userspace/vfs/src/vfs_service_core.c` | `astra_vfs_service_read_path`, `astra_vfs_service_read_into` — reads that answer into a buffer instead of a 192-byte reply record. |
| `sw/userspace/vfs/src/vfs_port_transport.c` | `astra_vfs_port_read_path` / `_read_borrow`: the caller uses the transfer area in place, no copy out. `READ_AREA` now does one backend read, not 86. Rebind replaces a bound area (it used to be refused as `BUSY`). |
| `sw/userspace/vfs/src/vfs_process.c` | One `LIBS:` sweep per process, not one per `OpenLibrary`; batched `readdir`; `read_file` and `astra_process_read_file` take the one-round-trip path. |
| `sw/userspace/supervisor/src/vfs_read.c`, `loader.c` | Program images are read in one round trip and launched straight out of the transfer area. Permanent `launch` line — §6. |
| `sw/userspace/runtime/src/memory.c` | `memcpy`/`memmove`/`memset` were **byte-at-a-time loops**. Now word copies. This is every byte the system moves. |
| `third_party/lwext4/src/ext4.c` | `ext4_fread` coalesces contiguous blocks into one device transfer, the way `ext4_fwrite` always did. |
| `sw/kernel/pmmu.S`, `pmmu.h`, `vm.c` | `kernel_pmmu_flush_page` — one page changed flushes one ATC entry, not all 22. |
| `emu/qemu/.../astra_pmmu030.c` | Single-page `PFLUSH` → `tlb_flush_page`. A CRP write no longer flushes the **kernel** index; kernel translations survive a process switch. |
| `sw/userspace/services/display/main.c` | Occlusion (`rect_subtract`, `visible_region`, `opaque_bounds`), undecorated windows compose from content, redundant initialise-fill skipped, canvas fill clipped to the work area, focus no longer repaints undecorated windows. §3a. |
| `sw/userspace/graphics/src/render_builder.c` | `astra_draw_list_covers`; `astra_render_builder_rounded` no longer paints its bands' intersection twice. |
| `fpga/arty/linux/astra_terminal_display.c` | `ASTRA_DISPLAY_PROFILE_COMMANDS=1` dumps per-command opcode, size, pixel count and hardware cycles from the completion records. This is how §3 was settled. |

Measured: program image read **313 → 76.6 ms for 100 KB**. A 16 KiB read went
31.2 → 15.3 ms, of which the copy fell 22.7 → 6.1 ms.

## 5. Things that did not work, so nobody repeats them

**Per-address-space MMU indices — built, measured, reverted (2026-08-20).**
§2 attributes the 7.5 ms round trip to the address-space switch, and §5 below
names the honest fix: address-space indices in TB flags. It was built. It
works, and it does not help.

What was built: `MMU_KERNEL_IDX` and `MMU_USER_IDX` kept, fourteen tagged
indices above them, a CRP-value-to-index LRU in `CPUM68KState`, the index in TB
flags bits 17-20, and the 43 `IS_USER(s)` sites in `translate.c` that were
*indices* split from the 22 that are *privilege tests* -- the two meanings the
one macro carried because `MMU_USER_IDX` happened to equal 1. `MMU_USER_IDX`
became the supervisor's window onto user memory for MOVES, flushed per switch;
the tagged indices are never flushed on a switch. It is sound because the
kernel flushes at every table edit and PFLUSH/PFLUSHA reach every index --
`sw/kernel/vm.c` flushes on every map and unmap regardless of which space it
edited.

What it measured, on one boot, same ROM and image:

| | untagged | tagged |
|---|---:|---:|
| guest page-table walks | 63,000 | 46,000 |
| TB count | 17,103 | 17,109 |
| TLB partial flushes | 885 | 911 |
| `SERVICES:storage` spawn, beast | 11.0-12.2 ms | 11.1-11.9 ms |
| the same, on `astra-arty` | 287-294 ms | 289-299 ms |

27% fewer page-table walks and **no wall-clock change on either machine**. The
tag table itself behaves perfectly -- 1,500 switches, 1,491 hits, 8 spaces
claimed, zero evictions -- so the mechanism is not the problem. The premise is:
**the ATC flush is not what a round trip costs.** A walk is cheap and there are
not many of them.

The patch is not in the tree. Rebuild it from this description only with a
measurement in hand that says the flush matters.

**Two more hypotheses killed the same day, so they are not re-run:**

- **The translation cache is not thrashing.** `info jit` after a boot:
  `TB flush count 0`, `TB invalidate count 2`, 17k TBs in 10 MB of a 1 GB
  buffer. Codegen is ~13% of a profile and TB lookup ~10%, but nothing is being
  thrown away -- only 65% of TBs are directly chained, which is where the
  lookup time goes.
- **The library cache's byte-for-byte compare is not the cost of a service
  launch.** `library_cache_match` re-reads every read-only page out of the
  launcher and compares 4096 bytes a page before it will use the cache, which
  looks exactly like the culprit. Short-circuiting it entirely changed
  `SERVICES:storage` spawn and ready by nothing at all -- that service maps no
  libraries, so the path never runs. It remains worth fixing for `fsopen`,
  which does open one, but it must be measured on a *Terminal* launch and not
  on a service.

**What is still unexplained:** `spawn=288737us` on the board to create a
process from a 77 KB image is ~113 cycles a byte at the 30 MHz baseline, where
a copy is 2-4. That number is the next thing to take apart, and it wants the
guest's own stage probes rather than another inference from the outside.

## 5a. Two things that did not work, so nobody repeats them

**Per-address-space MMU indices in the emulator were dead code.** m68k takes the
mmu index from TB flags (`IS_USER(s)` in `translate.c` yields 0 or 1), never
from `cpu_mmu_index`, so indices 2+ were never used by generated code. It was
removed. The improvement it appeared to give (storage spawn 612 → 267 ms) was
real but came from a different part of the same change — not flushing the
kernel index on a CRP write.

Doing it properly means putting the address-space index into m68k TB flags
(bits 17-19 are free) and changing every memory-access site in `translate.c`
that currently passes `IS_USER(s)` as an index. That is the honest fix for the
7.5 ms round trip, and it is real surgery.

**A transfer area that grows on demand measured worse than a fixed one.**
Display image read went 76 → 112 ms: the grow costs a rebind and a repeated
request, and it lands on exactly the large reads that matter. Reverted;
`VFS_PORT_AREA_MIN` is `ASTRA_VFS_BULK_MAX`. The rebind fix was kept because
refusing a rebind was a bug regardless.

**A storage image killed mid-run will not boot again until it is fsck'd.**
Killing QEMU rather than shutting down leaves a dirty ext4 journal; lwext4
replays it on the next boot and hands back bad bytes. It surfaces a long way
from the cause: `astra_launch:2: failed` (`ASTRA_SYSCALL_INVALID_ARGUMENT`) on
whichever service is read next — the supervisor exits `ASTRA_STATUS_INVALID`
(8) and the kernel panics with *initial user image exited*. The file on the
volume is **byte-identical and intact**; only the metadata is wrong, and
`e2fsck -fy` on the sliced-out volume fixes it with no file content touched.
Do that between runs, or splice a fresh image each time. Diagnosed 2026-08-19;
not fixed, and the real fix belongs in the shutdown or replay path.

**`BLOCK_MAX_SECTORS 256` panics the machine.** 128 (64 KiB) is fine. The
supervisor fails `verify_block_round_trip` with
`ASTRA_SUPERVISOR_FAIL_BLOCK_GEOMETRY`. Not diagnosed.

## 6. How to measure without touching anything

**`launch` line, permanent, in the shipped build.** One per program the
supervisor starts, covering any application:

```
launch APPS:Terminal.app bytes=45396 read=51412 spawn=36399 ready=788816us
```

`read` is the image off the volume, `spawn` is `PROCESS_CREATE`, `ready` is the
child's own start-up. Four clock reads and one log line, about 230 us against a
budget in the tens of milliseconds. `sw/userspace/supervisor/src/loader.c`.

**Boot launches five programs through the identical path**, so most tuning needs
no double-click at all — boot, then read the ring.

**Reading the ring** (the events service takes the console over early, so log
lines only reach the trace ring):

```sh
# on the board
python3 /data/astra/run/dumpring.py          # pmemsave 0x020c4000 65536 -> ring.bin
# on beast
python3 tools/trace_decode.py ring.bin | grep notice
```

Lines are chunked at 24 bytes; join the continuation records.

**Metrics contract.** `sw/include/astra/metrics.h` and `AstraOpMetrics` are the
project's intended home for per-module counters, but **nothing registers a group
and there is no cross-process reader yet**. Until the introspection filesystem
exists, the trace ring is the readout. Do not build a second one.

## 7. Ranked next steps

1. **The blitter's read path — `arlen = 0`.** §3. Blit costs 15-18 cycles a
   pixel against fill's 3.0, purely because each source beat is a separate,
   un-pipelined AXI read. Burst the source rows and prefetch. Every remaining
   millisecond in the compositor is this: the one required blit in a window
   drag is 32 ms and should be about 6. It is now the largest single item in
   the whole document.
2. **`fsopen`, 250 ms, identical on both launches.** Untouched, and now the
   biggest software item in a launch. Roughly sixteen round trips: a `HELLO`
   and a `BIND_AREA` per VFS client, then the `LIBS:` sweep. Two angles: fewer
   clients/binds per process, and skipping the sweep entirely when the library
   is already resident (see 3).
3. **Cross-process library sharing.** The kernel caches library pages
   (`library_cache` in `sw/kernel/process.c`) but keys the cache on a
   **byte-for-byte comparison of every read-only page**, so each process still
   reads the whole image off disk *and* the kernel re-compares it. Attaching by
   identity — the `AstraLibrary` record is already inside the kernel's 1024-byte
   header window at file offset 0x200 — removes both. Note
   `library_cache_create` skips writable segments, so those would need caching
   too. Authority stays intact if userspace still resolves through its own
   namespace and only skips the read.
4. **The 7.5 ms round trip.** §5, TB flags. Everything else is bounded by it.
5. **Finer damage than one rect.** Damage is a single union rectangle per
   framebuffer, so a drag damages everything between the old and new positions
   and a window open inherits whatever else that buffer accumulated. The
   occlusion machinery in §3a already works on rect lists; giving `DisplayState`
   a small damage rect list rather than one union would feed it directly. Worth
   less than 1 while blit costs 15 cyc/px, worth a lot after.

## 8. Machines and state

Build on `beast`; the Mac cannot build the kernel. Working trees used here:
`~/astra68-verify` (tracks the repo plus Terminal stage probes),
`~/astra68-final` (clean repo copy for gate runs). Both disposable.

The board is running the improved build from
`/data/astra/rom/astra_boot-probe.bin`, `/data/astra/storage-probe.img` and
`/data/astra/qemu/bin/qemu-system-m68k-astra-fix`. The originals
(`astra_boot.bin`, `storage-terminal.img`, `qemu-system-m68k-astra`) are
untouched — swap the three back to return to stock.

Deploying is: build userspace, build `sw/boot`, splice the volume with
`emu/qemu/astra_image.py`, copy ROM + image + QEMU to `/data/astra`, restart
`astra-terminal-start`. **`pkill` does not exist on the board** — kill by
explicit PID.

The board is currently running the compositor build from
`/data/astra/storage-fixed.img` with
`ASTRA_TERMINAL_DISPLAY=/data/astra/bin/astra-terminal-display.profcmd`. The
stock `astra-terminal-display` was never overwritten; the profiling build is a
separate file selected by that variable.

Gates green as of this handover: `sw/userspace` `make test` (on `beast` — the
Mac's linker has no `--gc-sections`, so `sw/userspace/services` cannot link its
host tests locally). Nothing is committed.
