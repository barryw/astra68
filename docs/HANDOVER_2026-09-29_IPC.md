# Handover 2026-09-29: kernel IPC call, and the road to SDL

Read `CLAUDE.md` and `AGENTS.md`, then this page, then `docs/IPC_CALL.md`
(the design you are about to build). `docs/HANDOVER_2026-09-28_GRAPHICS_PEAK.md`
holds the full record of the F2SDRAM and panic investigation. You only need it
for detail.

## Direction (user)

1. **Now:** get more performance from the kernel IPC path. Implement phase 1
   of `docs/IPC_CALL.md`: a one-shot reply capability plus a
   `PORT_CALL` syscall.
2. **Then:** a fully working SDL implementation with working SDL examples.
   SDL is Astra's non-native framework for porting games. The Astra
   underpinnings must be solid before more SDL work starts.

Standing rules: fix only the lowest tier (kernel, emulator, NDK, display
service, graphics libraries, SDL), never an individual program. No
band-aids. Gates stay green. Commit and push when a piece is verified.
Watch the board and report problems immediately.

## State at handover

**Git.** `main` is pushed.
- `c5d59368..82ab9926`: kernel, emulator, userspace, FPGA, docs.
- A final commit adds the copy-path and port-counter fixes, `IPC_CALL.md`
  and this page.

**Board (astra-de25).**
- Bitstream `68ce7bb2` (`rtl-noerr`). Its rollback bundle is
  `/var/lib/astra/boot-incoming-f2s`.
- Release `9ef3aebc`, published from `3c412b84` (the kernel fixes) after a
  full verify.
- `kernel.panic = 10`, so the board reboots itself after a panic.
- Certified on `b421dc7d`:
  - POST PASS.
  - Remote-desktop RFB PASS: `fpga/de25/linux/test_remote_desktop.py
    --password-file /etc/astra/remote-desktop.password --pointer 400 300
    --verify-rfb-pointer`, run on the board.
  - `astra-render-certify`: 9/9.
  - 15-minute SDLFrameBench soak: clean.

**Board fps** (SDLFrameBench 640x480): argb-blend **52** (42 on b421dc7d),
argb **55** (45), rgb565 **59-60** (53), with 0 helper failures, on
`9ef3aebc`. A frame is now upload about 7 ms (ARGB) or 5.3 ms (RGB565),
plus present 10.4-11.7 ms. **Present is now the biggest share of a board
frame.** Profile the present path (`WINDOW_PRESENT` compose, the display
service and the helper) alongside the IPC work.

**Beast fps** (`emu/qemu/bench-frame.py --fake-helper`):

| Mode | Morning | After the copy-path and port fixes |
|---|---|---|
| argb | 1,083 | 1,316 |
| argb-blend | 1,092 | 1,367 |
| rgb565 | 1,198 | 1,460 |

A frame is about 262k guest instructions, down from about 480k.

## What landed today (kernel, the final commit)

- **`area_copy_in` (AREA_COPY_IN, the texture-upload copy list)** was 62% of
  all guest instructions. For every chunk it re-probed the page tables and
  re-validated the area. It now:
  - validates the area range once (`kernel_area_range_live`);
  - looks up each source page and destination page once
    (`kernel_area_page_physical`, plus a one-entry cache on each side);
  - folds contiguous rows into a single span.

  The test in `test_process.c` covers a span that crosses a user page and an
  area page. Perturbation checked: without the page refresh the test fails.
- **`kernel_port_create`** scanned every port slot to update a statistic. It
  now keeps an `active_ports` counter, which the pool validator recounts.
  Perturbation checked.

## Next, in order

1. **IPC call, phase 1** (`docs/IPC_CALL.md`).
   - Kernel:
     - `KERNEL_OBJECT_REPLY`.
     - `ASTRA_SYSCALL_PORT_CALL`, taking an `AstraPortCall` descriptor.
     - Reply delivery inside `PORT_SEND_TRY` when the target is a reply
       capability: `kernel_vm_write` into the caller, then handle import,
       then the waker writes d0..d3.
     - The spent-capability semantics for `CLOSE`, and the abandon paths
       (deadline, cancel, death).
   - NDK:
     - `astra_port_call()`.
     - The 7 helpers that make a fresh reply port per call switch to it:
       - `ndk/src/graphics.c` `graphics_command`;
       - `window.c` `command` and `astra_window_create`;
       - `application.c`;
       - `service_manager.c`;
       - `clipboard.c`;
       - `pointer.c`.
   - ABI goes to `0x0001003A`.
   - Services do not change.
   - Model the tests on `test_wait_multiple_syscall_contract_and_races`
     in `sw/kernel/tests/test_process.c` for the two-thread cases.
   - Measure with `bench-frame.py` and profile, full verify, deploy, then
     measure on the board.
2. **The copy path is still the top profile item** (clean profile 4, all
   fixes):

   | Function | Share |
   |---|---|
   | `area_copy_in` | 14.3% |
   | `probe_root` | 6.6% |
   | `kernel_area_extents` | 3.8% |
   | `page_entries` | 3.7% |
   | `kernel_area_page_physical` | 2.2% |

   The block counts say the contiguous fold is **not** taken for SDL uploads.
   Find out why. `astra_surface_write` passes `area_pitch = row` and
   `source_pitch = pitch`, so SDL's texture pitch probably differs from the
   row. Fold, or copy by page, whatever the pitch. `kernel_area_extents`
   still runs 49M blocks per 20 s from another caller; the display
   attachment path is the likely one. Find it and fix it.
3. **Phase 2 of `IPC_CALL.md`**, if the numbers justify it. Then move to
   SDL.

## The measurement loop

- Profiling script: `beast:~/ipc-prof.sh`. It:
  - builds the `host-profile` QEMU;
  - builds the SDLFrameBench bundle, and runs `make -C sw/kernel clean`
    before rebuilding the ROM;
  - creates a 128 MiB image with the bench as the test app;
  - runs `bench-frame.py --fake-helper --span 20 --profile`;
  - produces the report with `astra-prof report --userspace-root
    sw/userspace --image kernel=sw/kernel/build/astra_kernel.elf`.

  Kernel hot spots sit at `0x0205xxxx` and above. Change the
  `frameN`/`reportN` names before each run.
- **Do not** run `make test` in `sw/kernel` while a ROM build or profile is
  running. The build directory is shared, and that race cost a profile
  today.
- Kernel host tests: `make clean && make test` in `sw/kernel` on beast. They
  cannot run on the Mac.
- Full verify: `~/astra-mg/verify-then-publish.sh`. It writes
  `/tmp/v-s5.log` and stops at `READY-TO-PUBLISH`.
- Publish and deploy: `beast:~/f2s-pub.sh`, which sources the verify
  environment and runs `emu/qemu/publish-de25-release.sh`.
- Board benchmark: `python3 ~/astra-mg/hw/board_bench.py SOCK
  /apps/SDLFrameBench.app SECONDS`, with the QMP socket forwarded as in
  `~/astra-mg/hw/run_bench.sh`. Pass the bundle as a full path. The app
  keeps running afterwards, so restart `astra` to close it.
- Helper profile on the board: `systemctl set-environment
  ASTRA_DISPLAY_PROFILE=1`, restart astra, then sum `render profile
  copy_us=... hardware_us=...` from the journal. Unset it afterwards.

## Hazards learned today

- **Any AXI SLVERR or DECERR on a CPU access panics the DE25**, as an
  SError. Since `68ce7bb2` the fabric answers OKAY and counts refusals in a
  fault record, but an old bitstream or a vendor address still panics.
  There is no serial console on beast.
- **F2SDRAM loses narrow read bursts over 128 beats.** The router splits
  them to 32 beats. Any new FPGA master that reads HPS DDR must do the
  same.
- **The DE25 helper runs its own batches** (terminal text, cursor). They
  must be staged in Media RAM with the aperture off
  (`select_render_aperture`). Guest batches run through the aperture.
- `pkill -f PATTERN` matches the invoking shell's own command line. Kill by
  PID, or by process name and age.
- When SSH hangs after authentication, `systemd-logind` on beast is wedged
  (`loginctl` times out). Restart logind or reboot beast.
- A software reboot of the board leaves RSTMGR HDSKREQ bit 0
  (EMIFFLUSHREQ) set. It is harmless: F2SDRAM works with it set.
