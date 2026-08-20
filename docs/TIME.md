# Astra 68 — what time it is

The machine knows the date. This is the whole chain, because every link of it
lives somewhere else and each one can be wrong on its own while looking fine
from either side.

```
  Linux, disciplined by NTP
        │
        │  QEMU_CLOCK_HOST
        ▼
  Vesta RTC_STATUS / RTC_NS_LO / RTC_NS_HI      emu/qemu/.../astra68.c
        │                                       sw/include/vesta.h
        │  kernel_platform_wall_clock_ns()      sw/kernel/platform.c
        ▼
  ASTRA_SYSCALL_CLOCK_REALTIME (55)             sw/kernel/process.c
        │
        ├── astra_clock_realtime()              sw/userspace/runtime
        │       ├── gettimeofday / clock_gettime    sw/userspace/posix/src/time.c
        │       ├── `date`                          sw/userspace/commands/date
        │       └── ext4_user_now()                 sw/userspace/storage
        │                └── inode atime/ctime/mtime  (lwext4 patch 0007)
        └── astra_civil_*()                     sw/common/civil.c
                └── the boot line, `ls -l`, `date`
```

## The decisions, and why

**The machine does not keep time; it reads it.** There is no software clock
counting from a boot-time sample, and no synchronisation protocol of Astra's
own. Every `CLOCK_REALTIME` reads the register at the moment it is asked, so a
correction made underneath — NTP stepping the host — is visible immediately
rather than being a drift this machine accumulated and never noticed. That is
also why the emulator and the board are the same code: on both, the thing
keeping time is a Linux clock somebody else is already keeping right.

**Nanoseconds since the Unix epoch, in 64 bits.** Not 32-bit seconds: a
seconds register is the 2038 problem compiled into the hardware contract, and
this project has spent enough of its life pulling ceilings back out of images.
Reading `RTC_NS_LO` latches both halves, so the pair cannot tear.

**Not knowing is an answer.** `RTC_VALID` distinguishes a machine whose clock
is running from one whose is not, and everything above it carries that
distinction rather than flattening it into a number:

| layer | no clock |
|---|---|
| `kernel_platform_wall_clock_ns` | returns false |
| `CLOCK_REALTIME` | `ASTRA_SYSCALL_UNSUPPORTED` (15) |
| `gettimeofday` | `-1` with `errno = ENOSYS` |
| `date` | *this machine has no clock*, exit 13 |
| lwext4 | writes 0, which is what it wrote before it had a clock |
| `ls -l` | `-` in the date column |
| boot line | `Wall clock ......... not set; files will have no dates` |

Zero is a real instant. A program that cannot tell "midnight in 1970" from "no
idea" writes the first one into a file and calls it a timestamp, and a listing
full of 1970 is worse than a listing full of dashes because it looks like data.

**UTC, and it says so.** There is no timezone database on this machine and
nothing has told it where it is. `date` prints `UTC` rather than implying a
local time it cannot support.

**One calendar.** `sw/common/civil.c` is compiled into the kernel and into the
userspace runtime, so the boot line, `ls -l` and `date` render an instant the
same way. It divides 64 by 32 the long way because the kernel links no libgcc:
`value / 86400` on a 64-bit value is an undefined reference to `__udivdi3`, and
a variable-count 64-bit shift is `__lshrdi3` for the same reason. The host test
sweeps 200,000 instants against `gmtime_r`.

**The filesystem's clock is bound, not called.** lwext4 asks the port through
`ext4_user_now()`; whoever mounts the volume binds it — the storage service,
which is the mount a program's writes reach, and the supervisor, which writes
its own boot check. The host mount test binds the host's clock and asserts the
timestamps land near now. A build with nothing bound behaves exactly as
upstream did: zeros.

## Where it is checked

| check | what it covers |
|---|---|
| `sw/kernel` `make test` → `CIVIL TIME PASS` | the calendar, 200k instants against the host's |
| `sw/kernel` `make test` → `platform tests passed` | the register read and the invalid case |
| `sw/kernel` `make test` → `process tests passed` | the syscall, including `UNSUPPORTED` |
| `sw/userspace/storage` `make ext4-test` | a written file and its directory carry a time near now |
| `emu/qemu/test-clock.py` | the whole chain: `date` against the host's clock, the three renderings agreeing, and a file written now listed with today's date |

The gate is the one that matters, and it fails four ways on a machine whose
`RTC_STATUS` reports invalid — which is how it was proved to be a gate rather
than a decoration.

## What is not here

- **Setting the time from Astra.** The register is read-only. The clock belongs
  to the layer below, which has NTP; a machine that could set it would be a
  machine that could disagree with the thing keeping it right.
- **Timezones and local time.** UTC everywhere. A timezone database is a real
  piece of work and nothing on this machine needs one yet.
- **`atime` on read.** lwext4 stamps access time at creation and nothing
  updates it afterwards. Relatime semantics would mean a metadata write per
  read, which on an SD card behind a 12.5 MHz bus is not a trade this machine
  should make silently.
- **A clock on hardware with no host.** `RTC_VALID` is what such a machine
  would report zero for, and everything above it already handles that.
