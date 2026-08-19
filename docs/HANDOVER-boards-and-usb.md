# Astra 68 — Handover: boards, and the devices on them

Date: 2026-08-19, later than `HANDOVER-memory-and-modernity.md`, which this
continues. Written to be read cold. Read `CLAUDE.md` first.

**Everything described here is committed and pushed.** `origin/main` is at
`98e32c4`, the working tree is clean, and the session's eight commits sit on
top of `c17cd1a`. That is a change from the previous handover, which opened by
saying nothing was committed — it is all in now.

---

## 1. Where this leaves the machine

The last session's theme turned out to be one sentence: **the size and shape of
the machine had been compiled into the image, in five separate places, and
every one of them was a board that no longer exists.** They came out one at a
time, each hidden behind the last:

| what was fixed | where it was | what it did |
|---|---|---|
| the supervisor identity map stopped at 32 MiB | `vm.c` | 96 MB of a 128 MB machine was classified usable, handed out, and **fatal on the write that zeroed it** |
| `KERNEL_MAX_FRAMES` and `uint16_t` frame links | `memory.c` | a board with more RAM than the constant refused to boot; a second ceiling waited at 256 MB |
| a two-value RAM whitelist | QEMU `astra68.c` | only 32 MiB or 128 MiB would start |
| a two-value RAM whitelist | `boot.c` | `POST FAIL: unsupported kernel RAM map` on anything else |
| the USB DMA aperture pinned to `0x03F00000` | `boot.c`, `platform.c` | the top of a 32 MiB board; a bigger board with USB could not have booted |

All five are gone. RAM size is a boot-info value the firmware reads from a
hardware register, the frame tables are carved from RAM at init and sized from
what the board reports, and the USB aperture comes from the controller.

`§5` and `§6.3`/`§6.4` of `HANDOVER-memory-and-modernity.md` carry the detail,
the measurements, and the reasoning for each. This document is the map
forward.

## 2. What is proved, and how

Worth stating precisely, because "the gates pass" was several times **not**
evidence of the thing it looked like evidence of.

- **RAM scaling.** The same ROM boots at 32, 64, 96, 128, 192, 256 and 512 MiB.
  The full terminal gate passes at 256 MiB — `ASTRA TERMINAL PASS 33 commands`
  — which is 65536 frames, exactly where the old `uint16_t` links died. A host
  test builds a gigabyte and allocates at frame indices the old build could not
  represent.
- **The USB branch is taken.** Free frames went 31698 → 31442 when the
  controller appeared: exactly 256 frames, the 1 MiB DMA pool, now classified
  device instead of free. And breaking the aperture in the model deliberately
  produced `POST FAIL: USB DMA aperture`, so the firmware is reading those
  registers and refusing a bad one.
- **The USB interrupt is real.** A throwaway probe armed the source and read
  back `ASTRA_STATUS=0x02` and `INTERRUPT_STATUS=0x04`, which `platform.c`
  composes into `0x20000004` — exactly `KERNEL_QUALIFICATION_IRQ_USB_EXPECTED`.
- **Every new test was checked against the defect it covers.** The hole-refill
  test fails against the old cluster scan; the DMA zone test fails with
  `KERNEL_DMA_ZONE_FRAMES` at zero; the device-aperture test fails without the
  cache-inhibit check.

## 3. The resume point

### 3.1 The qualification kernel does not boot under QEMU

**This is the first item, because it blocks the second.**

The K1–K10 device qualification in `kernel.c` is compiled out unless
`ASTRA_KERNEL_K1_QUALIFICATION=1`. Build it and boot it:

```
ssh beast 'cd ~/astra68-verify/sw/boot && make -j8 KERNEL_K1_QUALIFICATION=1'
```

and the machine panics before reaching it:

```
Reason: interrupt controller initialization failed
Worker: ... registered=0x00000039 ...
```

It panics **identically on an emulator build with no OHCI at all**, so this is
not a consequence of the USB work — it is a pre-existing condition of that
build under QEMU. Nobody has run it in the emulator for long enough to notice.

It matters because that program is the only thing that binds the USB IRQ, and
therefore the only thing that would put the interrupt path under a gate. Until
it boots, USB interrupts are covered by the probe in §2 and nothing else.

Start at `registered=0x00000039` — bits 0, 3, 4 and 5 — against what
`kernel_platform_qualification_irq_sources()` returns for this machine, and at
`kernel_interrupt_init`.

### 3.2 The USB interrupt path, once §3.1 boots

Nothing in a normal boot arms the USB source: the kernel brings the controller
up only for a process that binds that IRQ, and the desktop binds storage, input
and display. So the model's frame timer is idle in every gate today. With the
qualification kernel booting, `K10_QUALIFY_IRQ` arms it, waits, and checks the
record — and the model already produces what it expects.

Note the shape of the failure if it goes wrong: `K10_QUALIFY_IRQ` waits with
`ASTRA_DEADLINE_NONE`. An interrupt that never arrives is a boot that never
finishes, not a test that fails.

### 3.3 The DE25 Nano

The board is on its way, and the software should no longer care how much RAM it
has. What will still want checking, in this order:

1. **Its RAM size reaches the kernel.** `VESTA->RAM_SIZE` is read by the ROM;
   confirm the new platform reports it and that `Physical pages ..... N free /
   M total` matches the board.
2. **The USB aperture.** The firmware branch that reads
   `ASTRA_DMA_POOL_BASE`/`SIZE` from the controller now runs in the emulator,
   but a real OHCI is the first time it meets hardware that did not come from
   `astra68.c`. If its pool sits above the low 32 MiB, `vm.c` maps that 4 MiB
   span cache-inhibited — which is tested, but tested against a synthetic map.
3. **`KERNEL_DMA_ZONE_FRAMES`** is 128 frames sized from *this* machine's
   demand: 64 for the display's framebuffer and two per block transfer slot. A
   board with a different display or more transfer slots wants that number
   revisited rather than inherited.

### 3.4 Smaller things, all deliberate

- **The allocator returns empty runs immediately** rather than on a decay
  timer. jemalloc purges lazily so a program that allocates and frees in a loop
  does not thrash the fault path. Nothing here does that yet; the place for the
  timer is `give_pages` in `sw/userspace/posix/src/alloc.c`.
- **`heapbench` is in no gate.** Twenty thousand allocations is too slow for
  every build and its output is a number rather than a pass. Run it by hand
  when the allocator or `KERNEL_AREA_COMMIT_CLUSTER_PAGES` changes.
- **The POSIX debts** in `HANDOVER-memory-and-modernity.md` §6 are untouched:
  `rename` (an editor's atomic save needs it), `O_EXCL` (a documented TOCTOU
  race), `readdir` batching two entries, `opendir`'s pool of four, `access()`
  ignoring its mode, and the `ls` union member.

## 4. Traps this session cost real time to find

Each of these looked like a bug in the thing being changed and was not.

- **`rsync -a` preserves mtimes, so restoring a file on `beast` from the Mac
  can hand `make` a source older than an object built from something else.** It
  keeps the stale object. This produced a test failure identical to the bug
  that had just been fixed. `make clean` before believing a result you have
  gone back and forth on.
- **A raw `qemu-system-m68k` boot against a shared image is not a boot.** The
  gates copy the image and prepare it; a raw boot against `/tmp/storage-cmds.img`
  reaches `Reason: initial user image exited`, and an image killed mid-run stays
  dirty for the next attempt. Prepare a fresh copy per raw boot, or use a gate.
- **Passing gates are not evidence a new path is taken.** Adding the OHCI made
  all five pass, and they would have passed just as well if nothing had changed.
  Both USB claims in §2 needed a deliberate perturbation to become evidence.
- **`test_memory` builds without `KERNEL_MEMORY_HOST_TEST`** — it uses
  `KERNEL_MEMORY_NO_POISON`. Keying host-versus-kernel behaviour on the wrong
  macro made the host binary take the kernel path and write through a raw
  physical pointer. Key it on `defined(__m68k__)`: the kernel is only ever built
  for m68k, and a target that forgets a test macro is exactly how that happens.
- **The m68k kernel build carries `-Wcast-qual` and the host tests do not.** A
  cast that compiles in every host test can still fail the real build, and the
  intermediate `(void *)` in a chain is what discards the qualifier.

## 5. How to build and run all of it

Built on `beast` in `~/astra68-verify`, an rsync copy. The Mac cannot build the
kernel.

```
rsync -a --delete --exclude 'build*/' --exclude '*.o' \
      --exclude 'astra_kernel.*' --exclude 'astra_boot.*' --exclude 'astra68.rom' \
      sw/ beast:astra68-verify/sw/
rsync -a emu/ beast:astra68-verify/emu/

ssh beast 'cd ~/astra68-verify/sw/kernel    && make -j8 test'   # 30/30
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8 test'
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8'        # apps, for the volume
ssh beast 'cd ~/astra68-verify/sw/boot      && make -j8'        # ROM
```

The emulator needs rebuilding when `emu/qemu/qemu-9.2/hw/m68k/astra68.c`
changes — it is not rebuilt by the gates:

```
ssh beast 'cd ~/astra68-verify && sh emu/qemu/build.sh host'    # prints the binary path
```

Then, with `QEMU` set to that path and the image `/tmp/storage-cmds.img`:

```
for gate in test-display time-boot test-events test-terminal irq_quarantine_probe; do
  python3 emu/qemu/$gate.py $QEMU sw/boot/astra_boot.bin --image /tmp/storage-cmds.img
  echo "$gate=$?"
done
```

**Check the status, not the tail** — `... | tail` reports `tail`'s status.

`test-terminal.py` takes `--memory` now, so the machine can be proved at a size
other than the one the gate was written against:

```
python3 emu/qemu/test-terminal.py $QEMU sw/boot/astra_boot.bin \
    --image /tmp/storage-cmds.img --memory 256M
```

## 6. Where to read next

| Question | File |
|---|---|
| The memory work in full, with the numbers | `docs/HANDOVER-memory-and-modernity.md` |
| Complete inventory of everything | `docs/INVENTORY.md` |
| Project-wide continuation map | `docs/CURRENT_STATE.md` (mind the override) |
| ROM budget and memory layout | `docs/MEMORY_MAP.md`, `sw/include/astra/boot.h` |
| Debugging a program on the machine | `docs/DEBUGGING.md` |
