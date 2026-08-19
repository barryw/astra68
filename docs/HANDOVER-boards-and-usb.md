# Astra 68 — Handover: boards, and the devices on them

Date: 2026-08-19, later than `HANDOVER-memory-and-modernity.md`, which this
continues. Written to be read cold. Read `CLAUDE.md` first.

**Everything described here is committed.** The board-agnostic work is at
`311e924`; the K1-K10 qualification work in §3.1 is the single commit on top
of it, committed locally and not yet pushed.

**The short version of the second half:** the K1-K10 qualification kernel boots
again, runs every phase, and has a gate — `emu/qemu/test-qualification.py`. The
USB interrupt is inside it. Storage and input are present and deliberately not
claimed; §3.2 says what finishing them takes.

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

### 3.1 The qualification kernel boots, and its verdict is a gate

**Done.** `emu/qemu/test-qualification.py` boots the K1-K10 harness and reads
its verdict; it passes in under a second:

```
ssh beast 'cd ~/astra68-verify/sw/boot && make -j8 KERNEL_K1_QUALIFICATION=1'
python3 emu/qemu/test-qualification.py $QEMU sw/boot/astra_boot.bin \
    --image /tmp/storage-cmds.img
K10 partial, mask=0x00000380 in 0.2s
ASTRA QUALIFICATION PASS 12 markers
```

Five separate things were wrong, each hidden behind the last, and every one of
them was the same shape as the five in §1: **something the harness or the
kernel had compiled in about a machine that no longer exists.**

| what was wrong | where | what it did |
|---|---|---|
| the monitor's two IRQ sources were bound unconditionally | `interrupt.c` | the qualification build has no debug surface, so `kernel_monitor_init` never ran and the UART binding refused: `interrupt controller initialization failed` |
| `KERNEL_QUALIFICATION_SHARED_BASE` was `0x70000000` | `qualification.h` | that was the bottom of the first thread's stack when a stack was one page at the base of its slot. It is the **guard page** now, so both harness processes died on their first instruction |
| the harness was one page and had outgrown it | `process.c` | a raw image was refused above 4096 bytes: `survivor process creation failed` |
| the initial user image ran alongside the harness | `kernel.c` | with no capabilities it polls at NORMAL priority and starves the K6 thread at 15; the harness waited forever on a thread that was ready and never scheduled |
| the harness asserted a faulted process reports exit status 0 | `user_test.S` | it reports `ASTRA_STATUS_FAULTED` deliberately -- zero is what a program says when it *succeeded* -- so the sibling failed its own check and killed the process |

What each fix was:

- **The monitor is asked whether it exists.** `kernel_monitor_ready()`, and
  `kernel_interrupt_init` binds and arms its internal sources as one group
  through a loop, so the unwind is one loop rather than a ladder that grew a
  rung per source.
- **A raw process gets a data page**, `KERNEL_PROCESS_DATA_BASE`, at a fixed
  `0x00200000` -- fixed rather than after the image, because the image
  addresses it as a constant and would move every time it grew. The shared
  block is that page. A raw image is also mapped a page at a time now, up to
  `KERNEL_PROCESS_RAW_IMAGE_MAX`, so it is no longer confined to one page.
- **The qualification build does not start the initial image at all.** The
  harness is the whole workload; the special case that used to launch it
  without block capabilities is gone with it.
- **The harness produces its own quantum preemption.** The milestone asserts
  one happened, which needs two runnable threads of equal priority, which used
  to come from whatever else the machine was running. The survivor and the
  offender now both burn `KERNEL_QUALIFICATION_QUANTUM_SPIN_NS` -- 12 ms, two
  quanta and a margin -- reading the clock rather than counting iterations,
  because the 32768 iterations that lasted a quantum on a 30 MHz 68030 last a
  fraction of one under the emulator and the preemption never happened.

### 3.2 What the mask means, and what is still not proved

`mask=0x00000380` is USB, Vega and Astraea. **The USB interrupt is now under a
gate**: armed, delivered, read, checked against
`KERNEL_QUALIFICATION_IRQ_USB_EXPECTED`, consumed, acknowledged, and its
endpoint closed. Breaking that expected value deliberately produces no `K1
PROTECTED ENTRY PASS`, so the check is real and not decorative.

Storage and input are **present and not qualified here**, and that is now said
rather than claimed. Their `prepare` asks the device for an interrupt that is
already pending: a storage state change, or an input event. On the ULX3S the
AstraHost link's host end produced both on demand; under the emulator nothing
plays that part, so `prepare` answers `IO_ERROR`, the harness closes the
endpoint and leaves the source out of the mask it reports. `COMPLETE_IRQS`
takes a subset of what was authorized, and the kernel prints what was proved.

Two ways to finish those two, for whoever picks this up:

1. **Give the emulator the host end.** Media change for storage (the model
   already has `state_change`, set once at reset and consumed at boot by
   `refresh_device_state`), and an injected input event for input. The input
   check also asserts an event identity -- class `0x7e`, value `"ASTR"` --
   that only that link ever produced; a real keypress would not match it, so
   the check has to become "the event the interrupt announced is the event I
   consumed" or the injection has to carry those fields.
2. **Provoke storage from inside.** A completion interrupt from a benign read
   is what the device does in normal traffic and needs no host at all -- but
   the qualification's `consume` currently demands a state change and refuses
   a completion, so that is a contract change, not a patch.

On the DE25 with a real host, neither is in the way of the other three.

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
- **`sw/kernel/build/` holds host objects and m68k objects in turn.** Build the
  ROM, then run `make test` in the same tree, and the host binaries are linked
  from m68k objects: `./build/test_mmio: cannot execute binary file`, which
  reads as a broken test and is a stale tree. `make clean` between the two.
- **A gate that is not the whole answer will happily pass.** The qualification
  boots for two seconds and then idles; every intermediate state in §3.1 --
  harness dead, harness deadlocked, harness silently exited -- looked identical
  from the serial log, which said nothing after the last boot line. What broke
  it open was `qemu -S -s` and `gdb-multiarch` on `astra_kernel.elf`: breakpoints
  on `retire_current`, `retire_current_thread` and `complete_wait` name the
  thread that died and where, and `kernel_process_stats` can be *called* from
  gdb into scratch RAM to see which milestone counter is still zero. Attach at
  2 s and it is already over -- use `-S`.
- **`-serial file:` writes nothing here.** Redirect `-serial stdio` to a file
  instead, or a gdb run looks like a machine that never booted.
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

The qualification gate is a **different ROM** and runs on its own:

```
ssh beast 'cd ~/astra68-verify/sw/boot && make -j8 KERNEL_K1_QUALIFICATION=1'
python3 emu/qemu/test-qualification.py $QEMU sw/boot/astra_boot.bin \
    --image /tmp/storage-cmds.img          # --mask 0x380 is the default
```

Rebuild the normal ROM afterwards -- both write `sw/boot/astra_boot.bin`.

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
