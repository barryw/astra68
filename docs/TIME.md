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
        RTC_UTC_OFFSET / RTC_ZONE                sw/include/vesta.h
        │
        │  kernel_platform_wall_clock()         sw/kernel/platform.c
        ▼
  ASTRA_SYSCALL_CLOCK_REALTIME (55)             sw/kernel/process.c
        │
        ├── astra_clock_realtime_zone()         sw/userspace/runtime
        │       ├── astra_datetime_now()            ndk/src/datetime.c
        │       ├── gettimeofday / clock_gettime    sw/userspace/posix/src/time.c
        │       ├── `date`                          sw/userspace/commands/date
        │       └── ext4_user_now()                 sw/userspace/storage
        │                └── inode atime/ctime/mtime  (lwext4 patch 0007)
        └── astra_civil_*()                     sw/common/civil.c
                └── the boot line, `ls -l`, `date`
```

On Arty, `S02astra-firstboot` runs after networking and waits without a failure
timeout until its SNTP client has set a valid Linux wall clock. When eth0 has no
IPv4 address it retries the board's normal `ifdown`/`ifup` configuration before
the next SNTP attempt, so a failed initial DHCP exchange cannot strand the gate.
HDMI, the boot splash, QEMU, and therefore Axiom cannot start earlier. Each
attempt replaces the bounded diagnostic log rather than growing persistent
storage while the network is unavailable.

## What a person types

```
date                    Wed Aug 19 20:52:41 EDT 2026      the machine's zone
date -u                 Thu Aug 20 00:52:41 UTC 2026
date -I                 2026-08-19
date -Is                2026-08-19T20:52:41-04:00
date -uIs               2026-08-20T00:52:41+00:00
date -R                 Wed, 19 Aug 2026 20:52:41 -0400
date +%H:%M             20:52
date +%Y-%m-%dT%H:%M:%S%Z
date -e / date +%s      1787187161
```

`+FORMAT` is strftime's, because that is what the person typing already knows.
Two specifiers are substituted before strftime sees them: picolibc's `struct
tm` carries no zone at all -- its `%Z` reads a global that only `tzset()` and a
`TZ` environment variable can fill, and this machine has neither -- so `%Z` and
`%z` come from the instant, which knows its own zone.

A format containing a space has to wait for the shell to learn quoting: every
word is its own argument today, and `date` says so rather than silently
rendering the first word.

`date -s` is refused. The clock is read-only here by design.

## What an application asks

```c
#include <astra/datetime.h>

AstraDateTime now;

if (astra_datetime_now(&now)) {
    char text[64];

    astra_datetime_format(&now.local, "%H:%M %Z", text, sizeof(text));
}
```

One reading gives the instant, the zone, and both renderings -- `now.local`
and `now.utc` -- so a window showing a clock and a log recording UTC never
disagree about which second they meant.

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

**Host time is a boot contract.** `RTC_VALID` distinguishes a machine whose
clock is running from one whose is not. The platform primitive and syscall
retain an explicit failure result for testability, but a production kernel now
panics during boot with `valid host wall clock required` if the bit is absent.
The QEMU terminal gate checks the register independently before opening a
Terminal, and the Arty launcher refuses a stale Linux clock. Astra therefore
never reaches userspace with an invented epoch or a silently missing date:

| layer | no clock |
|---|---|
| `kernel_platform_wall_clock_ns` | returns false |
| `CLOCK_REALTIME` | `ASTRA_SYSCALL_UNSUPPORTED` (15) |
| `gettimeofday` | `-1` with `errno = ENOSYS` |
| `date` | *this machine has no clock*, exit 13 |
| lwext4 | writes 0, which is what it wrote before it had a clock |
| `ls -l` | `-` in the date column |
| production boot | kernel panic before userspace |

Zero is a real instant. A program that cannot tell "midnight in 1970" from "no
idea" writes the first one into a file and calls it a timestamp, and a listing
full of 1970 is worse than a listing full of dashes because it looks like data.

**The zone is an offset and a name, not a rule set.** `RTC_UTC_OFFSET` and
`RTC_ZONE` carry the offset in force *now* -- summer time already decided -- and
the abbreviation to print beside it, both from the layer that keeps the clock.
That layer has tzdata and already applies it; a second copy on this side would
be a second answer that can disagree with the first, and it would need updating
every time a government moves a date. Under the emulator the zone is the host
process's: `TZ=America/New_York qemu-system-m68k ...` and the machine is in
EDT, offset and abbreviation and all.

A machine with a clock and no location reports `RTC_ZONE_VALID` clear, and
everything above it renders UTC and says `UTC` rather than implying a place.

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
| `emu/qemu/test-clock.py` | the whole chain: `date` against the host's clock, `-u`, `-uIs` and `+FORMAT` agreeing, `%Z` rendering a real zone, and a file written now listed with today's *local* date. Run it under `TZ=America/New_York` as well as UTC -- both pass, and the second is the one that proves the zone is not decoration |

The gate is the one that matters. On 2026-08-24 it measured guest epoch
`1787595485` against host epoch `1787595484` with zero-second rounded drift;
the active event-overwrite candidate's second physical Arty boot reported
`2026-08-24T19:59:32Z, from the host`; a live `date -e` completed successfully
after that reboot.

## What is not here

- **Setting the time from Astra.** The register is read-only. The clock belongs
  to the layer below, which has NTP; a machine that could set it would be a
  machine that could disagree with the thing keeping it right.
- **A timezone database.** Deliberately absent -- see above. What that costs is
  historical conversion: the machine can render *now* in its zone, but not "what
  was the offset here in 1997", because nothing on this side knows the rules.
  Rendering an old file's timestamp uses today's offset.
- **`TZ` for POSIX code.** picolibc's `localtime` and `%Z` read a `TZ`
  environment variable this machine does not have, so C code that wants local
  time uses `astra/civil.h` or `astra/datetime.h` rather than `localtime`.
  `gettimeofday` and `clock_gettime` are UTC, which is what they are for.
- **`atime` on read.** lwext4 stamps access time at creation and nothing
  updates it afterwards. Relatime semantics would mean a metadata write per
  read, which on an SD card behind a 12.5 MHz bus is not a trade this machine
  should make silently.
- **A clock on hardware with no host.** That is not an Astra production
  configuration; it fails the boot contract instead of running with bad time.
