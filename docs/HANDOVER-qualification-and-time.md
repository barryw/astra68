# Astra 68 — Handover: the qualification gate, the listing, and the clock

Date: 2026-08-20, continuing `HANDOVER-boards-and-usb.md`. Written to be read
cold. Read `CLAUDE.md` first, then `docs/TIME.md` if you are anywhere near a
timestamp.

**Everything here is committed and pushed.** `origin/main` is at `83d806b`;
five commits sit on top of `311e924`:

| commit | what |
|---|---|
| `addfc42` | the K1–K10 device qualification boots again, and has a gate |
| `0c312fb` | `ls` tells the truth: dot entries, relative paths, `-a` |
| `9f68a8e` | the machine knows what day it is |
| `3576251` | `date` with formats, a timezone, and an NDK clock |
| `83d806b` | the in-tree library a freestanding kernel needs |

---

## 1. Where this leaves the machine

Three things that were broken or absent are now working and gated.

**The device qualification runs.** `emu/qemu/test-qualification.py` boots the
K1–K10 harness and reads its verdict in under a second. The USB interrupt is
inside it — armed, delivered, read, checked, consumed, acknowledged, endpoint
closed — which was the open item the previous handover left.

**A listing says what it knows.** `ls -l` had shown `d?????????` with no owner,
size or date for `.` and `..`, and only in directories the machine itself had
created. `ls foo` refused a directory `mkdir foo` had just made. Both fixed;
dot entries are hidden unless `-a`, like every other `ls`.

**The machine has a clock.** Nothing on it had a date before: lwext4 wrote zero
into every timestamp of every inode it created, and there was no way to ask the
machine the time at all. The whole chain is in `docs/TIME.md` — the short
version is that the clock is the host's, kept right by NTP, read at the moment
it is asked, and *not knowing* is carried as an answer at every layer rather
than flattened into 1970.

## 2. What is proved, and how

Every claim below was made to fail on purpose before it was believed. That is
the only reason to trust any of them.

- **The USB interrupt path.** `K10 partial, mask=0x00000380` — USB, Vega and
  Astraea. Breaking `KERNEL_QUALIFICATION_IRQ_USB_EXPECTED` produces no `K1
  PROTECTED ENTRY PASS`, so the record really is checked.
- **The dot-entry fix.** A host probe against the real volume showed
  `meta(/vol/work/.) rc=2` before and `rc=0` after; `make ext4-test` fails
  against the unpatched library with `FAIL /volume/many/. rc=2`.
- **The clock.** `test-clock.py` requires `date` to agree with the host's clock
  (drift 0 s), the renderings to agree with each other, and a file written a
  second earlier to be listed with today's date. On a machine whose
  `RTC_STATUS` reports invalid it fails four ways, and the boot line reads
  *not set; files will have no dates*.
- **The timezone.** The same gate passes under `TZ=America/New_York` as well as
  UTC, and the second run is the one that proves the zone is not decoration:
  `date` says EDT, `-0400`, and `ls -l` shows the local day.
- **The calendar.** 200,000 instants against the host's `gmtime_r`, plus the
  zone cases that cross a day, a month, a year and a half-hour offset.
- **The formatter and the divide.** Both checked against the host's own
  implementations — `snprintf` specifier by specifier, and native 64-bit
  division at every power of two and either side of it.

## 3. The resume point

### 3.1 Storage and input are still not qualified

`mask=0x380` is three of five. Storage and input are **present and
deliberately not claimed**: their `prepare` asks the device for an interrupt
that is already pending, and under the emulator nothing plays the part the
AstraHost link's host end played. `HANDOVER-boards-and-usb.md` §3.2 has the two
ways to finish them, and neither is in the way of the other three.

### 3.2 The shell cannot quote — **wrong, and done**

**This section was false when it was written.** The shell has always quoted:
`astra_shell_parse` handles `'`, `"` and `\`, `run_line` calls it, and
`date +"%H %M"` prints `01 26` on the real ROM. `date` carried the same false
claim in its error text and no longer does.

Redirection was the half that was genuinely missing, and it is done —
`ls > out.txt`, `date >> log`, both gated. See
`docs/HANDOVER-shell-redirection.md`, which also has the filesystem bug this
uncovered: every append on the machine silently truncated. What is left there
is `<`, `2>` and pipes.

### 3.3 The rest of `console_printf`

The kernel has a printf now and about 480 `console_puts`/`console_dec32`
call sites still do it the old way. Converting them is mechanical, one block at
a time, and roughly ROM-neutral: the formatter cost ~700 bytes and each dense
block returns a few tens. 8,864 bytes free of 262,144 today. The panic dump and
the boot report are done; `kernel_process_milestone_reached` is the largest
block left.

### 3.4 Time, the parts deliberately absent

- **No timezone database**, so an *old* file's timestamp renders with today's
  offset. Fixing that means tzdata on this side, which is exactly the second
  copy of the rules `docs/TIME.md` argues against — think before doing it.
- **`TZ` for POSIX code.** picolibc's `localtime` and its `%Z` read an
  environment variable this machine does not have. C code that wants local time
  uses `astra/civil.h` or the NDK's `astra/datetime.h`. If a program ever needs
  `localtime`, the honest fix is an environment, not a fake global.
- **No `atime` on read**, and no way to set the clock from Astra. Both are
  decisions, not omissions; the reasons are in `docs/TIME.md`.

### 3.5 The board

`HANDOVER-boards-and-usb.md` §3.3 is untouched: the DE25 Nano still wants its
RAM size, USB aperture and DMA zone checked when it arrives. Add one item to
that list now — **the board's wall clock**. `RTC_STATUS`, `RTC_UTC_OFFSET` and
`RTC_ZONE` come from the QEMU process on the board too, so its Linux side must
be NTP-synced and know its zone, or every file written there is stamped from a
clock nobody set.

## 4. Traps this session cost real time to find

- **`rsync -a` preserves mtimes, and `make` believes them.** Restoring the tree
  on `beast` handed `make` sources older than objects built from something
  else, and the stale userspace it kept produced a supervisor that faulted at
  partition read — a failure that looks exactly like a kernel bug. `make clean`
  in `sw/userspace` *and* `sw/kernel` after any rsync you have gone back and
  forth over. This is in `CLAUDE.md` and it still cost twenty minutes.
- **`sw/kernel/build/` is shared by the host tests and the m68k build.** Build
  the ROM, then run `make test`, and the host binaries link from m68k objects:
  `./build/test_mmio: cannot execute binary file`.
- **Two processes mount the volume.** The supervisor mounts it at boot to
  verify and then unmounts; `sw/userspace/services/storage` is the mount a
  program's writes actually reach. Binding the filesystem's clock in the
  supervisor alone left every file undated, and everything about the binding
  looked correct.
- **`emu/qemu/build.sh` defaults its work root to the NAS, whose clock runs
  ahead of `beast`'s.** meson refuses with `Clock skew detected`. Use
  `ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4`.
- **`-serial file:` writes nothing here.** A gdb run then looks like a machine
  that never booted. Redirect `-serial stdio` instead.
- **Attach the debugger before the machine runs.** The qualification boots and
  dies inside two seconds; `qemu -S -s` and breakpoints on `retire_current`,
  `retire_current_thread` and `complete_wait` name the thread that died and
  where. `kernel_process_stats` can be *called* from gdb into scratch RAM to
  see which milestone counter is still zero.
- **picolibc's `struct tm` has no zone fields at all.** Its `%Z` reads a global
  that only `tzset()` and a `TZ` environment variable fill, so `%Z` and `%z`
  are substituted from the instant before `strftime` sees the format.
- **The terminal gate's typist could not type capitals or `+`.** QMP's qcodes
  are the unshifted key names; a capital is the shift chord. Fixed in
  `test-terminal.py`, and every gate uses that typist.

## 5. How to build and run all of it

On `beast`, in `~/astra68-verify`. The Mac cannot build the kernel.

```
rsync -a --delete --exclude 'build*/' --exclude '*.o' \
      --exclude 'astra_kernel.*' --exclude 'astra_boot.*' --exclude 'astra68.rom' \
      sw/ beast:astra68-verify/sw/
rsync -a emu/ ndk/ third_party/lwext4/ beast:astra68-verify/...

ssh beast 'cd ~/astra68-verify/sw/kernel    && make clean && make -j8 test'  # 33
ssh beast 'cd ~/astra68-verify/ndk          && make -j8'
ssh beast 'cd ~/astra68-verify/sw/userspace && make clean && make -j8 test'
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8'
ssh beast 'cd ~/astra68-verify/sw/userspace/storage && make ext4-test'
ssh beast 'cd ~/astra68-verify/sw/kernel    && make clean'   # host objects out
ssh beast 'cd ~/astra68-verify/sw/boot      && make clean && make -j8'
```

The emulator, when `hw/m68k/astra68.c` changes:

```
ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4 sh emu/qemu/build.sh host
```

Then the gates, with `$QEMU` set to the path that printed:

```
for gate in test-display time-boot test-events test-terminal \
            irq_quarantine_probe test-clock; do
  python3 emu/qemu/$gate.py $QEMU sw/boot/astra_boot.bin --image /tmp/storage-cmds.img
  echo "$gate=$?"
done

# and once more in a zone that is not UTC, which is the point of the last one
TZ=America/New_York python3 emu/qemu/test-clock.py $QEMU sw/boot/astra_boot.bin \
    --image /tmp/storage-cmds.img
```

The qualification is a **different ROM** and runs on its own:

```
ssh beast 'cd ~/astra68-verify/sw/kernel && make clean'
ssh beast 'cd ~/astra68-verify/sw/boot   && make -j8 KERNEL_K1_QUALIFICATION=1'
python3 emu/qemu/test-qualification.py $QEMU sw/boot/astra_boot.bin \
    --image /tmp/storage-cmds.img     # --mask 0x380 is the default
```

Rebuild the normal ROM afterwards — both write `sw/boot/astra_boot.bin`, and
`make clean` in `sw/kernel` between them or the qualification objects stay.

**Check the status, not the tail** — `... | tail` reports `tail`'s status.

## 6. Where to read next

| Question | File |
|---|---|
| The clock, the zone, and what has a date | `docs/TIME.md` |
| Boards, USB, and the qualification's own history | `docs/HANDOVER-boards-and-usb.md` |
| The memory work in full | `docs/HANDOVER-memory-and-modernity.md` |
| Complete inventory of everything | `docs/INVENTORY.md` |
| The vendored filesystem and its seven patches | `third_party/lwext4/ASTRA_VENDOR.md` |
| ROM budget and memory layout | `docs/MEMORY_MAP.md`, `sw/include/astra/boot.h` |
| Debugging a program on the machine | `docs/DEBUGGING.md` |
