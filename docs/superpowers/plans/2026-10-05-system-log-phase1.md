# System Log Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every program's log output — desktop apps (Chocolate Doom first),
services, supervisor and kernel — lands in one time-ordered text file,
`/events/syslog` on the boot volume, and a live copy goes to the host. No
syscall, lock or wait is added per log line.

**Architecture:**

- **Time page.** The kernel publishes monotonic and wall time on a read-only
  page mapped in every process.
- **Log ring.** Each process lazily creates one lock-free multi-writer log ring
  in a shared area. It hands the consumer end to the events service in one
  non-blocking port send.
- **Service.** The events service runs below application priority. It drains
  every ring and the kernel trace ring, formats one line per record, and
  appends with size and midnight rotation.
- **Reading.** A `log` command filters the files.

Design: `docs/SYSTEM_LOG.md` (read it first).

**Tech Stack:** C11 freestanding (kernel, runtime), picolibc/POSIX layer,
MC68040 cross toolchain (`m68k-astra-gcc` on `beast`), host tests with plain
`assert`, Python QEMU gates.

## Global Constraints

- Logging never blocks and never slows a program. Requirement 2 of
  `docs/SYSTEM_LOG.md`.
  - No syscall per line, no lock, and no wait on the service or on another
    thread.
  - A full ring drops the line and counts it.
- The runtime (`sw/userspace/runtime`) must not call the system library
  (`sw/userspace/system`): the system library links against the runtime.
  Runtime code uses `astra_rt_*`, `astra_port_send` and `astra_syscall5` only.
- `runtime.library` has no `_Thread_local` (`runtime/src/syscall.c:25-27`).
  `libc.library` (`sw/userspace/posix`) does.
- CAS on MC68040: use `__atomic_compare_exchange_n` in an explicit loop. Never
  use `__atomic_exchange_n` (see `ndk/include/astra/runtime.h:785-800`).
- The kernel links no libgcc: no 64-bit `/`, no variable 64-bit shifts, no
  `= {0}` on word arrays (see `CLAUDE.md`). Use `astra_divide_u64` and
  `kernel_bytes_*`.
- **The events service never logs** (`services/events/main.c:5-9`). It must not
  include `astra/event_emit.h` or call `astra_log*`.
- **Build machine.** Kernel, ROM, m68k and gate builds run on `beast`, one at a
  time, via `~/astra-mg/locked <command>`.
  - Sync the source tree first with:
    `rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68/`
  - Build userspace before `sw/boot` (`CLAUDE.md`).
  - Rebuild QEMU only if `astra68.c` changes; this plan does not change it.
- `make clean` in `sw/kernel` between the m68k build and `make test`. They share
  `sw/kernel/build/`.
- Never commit with failing tests. Each task ends green on beast.
- Record layout (shared by producer and consumer, `sw/include/astra/log_ring.h`):
  - area 65,536 bytes
  - ring at offset 0
  - 256 slots of 252 bytes (`64 + 256*252 = 64,576`)
  - each slot = 4-byte commit word + 248 payload bytes
  - first slot payload = 40-byte record header + 208 text bytes; each
    continuation slot carries 248 text bytes
  - text max 1,944 bytes (8 slots); longer text is truncated and flagged
- Line format, one record per line:
  `YYYY-MM-DDTHH:MM:SS.mmm+HH:MM level ident[pid-hex] stream: text\n`.
  - Non-printable bytes are written as `\xNN`.
  - Levels are `debug info notice warning error`.
  - Streams are `log stdout stderr kernel`.
- Rotation (phase 1, plain text): rotate at 1,048,576 bytes or at the first
  write on a new local date. Keep `syslog.1`..`syslog.3`. Compression is
  phase 2.

---

## File Structure

| File | Responsibility |
|---|---|
| `sw/include/astra/time_page.h` (new) | Time page ABI: struct, magic, address, inline reader |
| `sw/include/astra/address_space.h` | New `TIME_PAGE` region carved from the reserved window |
| `sw/kernel/time_page.c`, `time_page.h` (new) | Kernel frame, publish, map, update |
| `sw/kernel/vm.c` | Admit the time-page region for shared-frame mappings |
| `sw/kernel/process.c` | Map at launch and exec; update on tick and switch |
| `sw/include/astra/log_ring.h` (new) | Log ring ABI: slot layout, record header, setup message |
| `sw/userspace/runtime/src/log_ring.c` (new) | Producer (lock-free reserve, commit, publish, notify) and consumer decode. Pure, host-tested |
| `sw/userspace/runtime/src/log.c` | `astra_log_line`, lazy setup, fallback; existing `astra_log*` route through it |
| `sw/userspace/runtime/src/time_page.c` (new) | `astra_time_now` with fixed-address page and syscall fallback |
| `sw/userspace/events/src/log_format.c` (new) | Record → text line |
| `sw/userspace/events/src/log_file.c` (new) | Append, rotation and boot marker over injected I/O |
| `sw/userspace/events/src/log_clients.c` (new) | Client table, drain, merge by time, drop markers |
| `sw/userspace/events/src/event_backend.c` | `EVENTS:log/syslog` read-through |
| `sw/userspace/services/events/main.c` | LOG port, ring waits, file sinks, priority |
| `sw/userspace/supervisor/src/loader.c` | Universal `LOG` grant |
| `sw/userspace/services/terminal/console_session.c` | Forward `LOG` to commands |
| `emu/qemu/astra_image.py` | Manifest: events serves `LOG`, grants `WORK:rw`, `priority=12`; terminal ceiling |
| `sw/userspace/posix/src/console.c` | fd 1/2 as separate log descriptions, per-thread line assembly, flush in `_exit` |
| `sw/userspace/commands/log/` (new) | `log show`, `log stream`, filters |
| `sw/userspace/commands/logburst/` (new) | Test program: burst, timing and fault |
| `emu/qemu/test-system-log.py` (new) | QEMU gate |
| Docs | `ABI.md`, `MEMORY_MAP_AND_PMMU.md`, `DEBUGGING.md`, `CURRENT_STATE.md` |

---

### Task 1: Time page ABI and userspace reader

**Files:**
- Create: `sw/include/astra/time_page.h`
- Create: `sw/userspace/runtime/src/time_page.c`
- Create: `sw/userspace/runtime/tests/test_time_page.c`
- Modify: `sw/include/astra/address_space.h` (new region)
- Modify: `sw/userspace/runtime/Makefile` (source, test), `sw/userspace/runtime/runtime.library.exports`

**Interfaces:**
- Produces: `AstraTimePage`, `AstraTimeSample`,
  `astra_time_page_read(const volatile AstraTimePage *, AstraTimeSample *) -> int`
  (inline), `astra_time_now(AstraTimeSample *) -> void`,
  `ASTRA_TIME_PAGE_ADDRESS`, `ASTRA_TIME_PAGE_MAGIC`.
- Consumed by Task 2 (kernel writer) and Task 4 (log producer).

- [ ] **Step 1: Add the address region**

In `sw/include/astra/address_space.h`, replace the reserved-window definitions
with a one-page time region followed by the reserved remainder:

```c
/** First byte of the kernel time page, mapped read-only in every process. */
#define ASTRA_TIME_PAGE_ADDRESS_START         ASTRA_SHARED_AREA_ADDRESS_END
/** One past the time page. */
#define ASTRA_TIME_PAGE_ADDRESS_END           0x48001000u
/** First byte of the reserved window. */
#define ASTRA_RESERVED_ADDRESS_START          ASTRA_TIME_PAGE_ADDRESS_END
```

Add the region to `ASTRA_USER_ADDRESS_REGIONS` between `SHARED_AREAS` and
`RESERVED`:

```c
    X(ASTRA_ADDRESS_REGION_TIME_PAGE, time_page,                           \
      ASTRA_TIME_PAGE_ADDRESS_START, ASTRA_TIME_PAGE_ADDRESS_END,          \
      "kernel time page")                                                 \
```

In the contiguity `_Static_assert`, replace
`ASTRA_SHARED_AREA_ADDRESS_END == ASTRA_RESERVED_ADDRESS_START` with:

```c
               ASTRA_SHARED_AREA_ADDRESS_END == ASTRA_TIME_PAGE_ADDRESS_START &&
               ASTRA_TIME_PAGE_ADDRESS_END == ASTRA_RESERVED_ADDRESS_START &&
```

`sw/kernel/tests/test_address_space.c` walks the X-macro and needs no edit.

- [ ] **Step 2: Write `sw/include/astra/time_page.h`**

```c
#ifndef ASTRA_TIME_PAGE_H
#define ASTRA_TIME_PAGE_H

/** @file time_page.h
 * @brief The kernel time page: monotonic and wall time without a syscall.
 *
 * Haiku's real_time_data (src/system/kernel/real_time_clock.cpp:108, read by
 * src/system/libroot/os/time.cpp:29): one kernel page mapped read-only into
 * every process. The kernel writes the copy readers are not using and then
 * advances `sequence`; a reader takes the copy `sequence` names and retries
 * if `sequence` moved while it read. The values are as of the last timer tick
 * or context switch, so they trail the clock registers by at most one quantum
 * (5 ms). Callers that need the register's 80 ns resolution keep using the
 * clock syscalls.
 */

#include <stdint.h>

#include <astra/address_space.h>

#define ASTRA_TIME_PAGE_ADDRESS ASTRA_TIME_PAGE_ADDRESS_START
#define ASTRA_TIME_PAGE_MAGIC   0x54494D45u /* "TIME" */
#define ASTRA_TIME_PAGE_VERSION 1u
/** Set in `flags` when the wall-clock fields are valid (RTC_STATUS valid). */
#define ASTRA_TIME_PAGE_WALL_VALID 0x1u

typedef struct AstraTimePageCopy {
    uint32_t monotonic_high;   /* ns since boot */
    uint32_t monotonic_low;
    uint32_t wall_high;        /* ns since 1970-01-01T00:00:00Z */
    uint32_t wall_low;
    int32_t  utc_offset;       /* seconds east of UTC */
    uint32_t zone;             /* packed 4-character zone name */
    uint32_t flags;            /* ASTRA_TIME_PAGE_WALL_VALID */
    uint32_t reserved;
} AstraTimePageCopy;

typedef struct AstraTimePage {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;         /* copy[sequence & 1] is current */
    uint32_t reserved;
    AstraTimePageCopy copy[2];
} AstraTimePage;

_Static_assert(sizeof(AstraTimePageCopy) == 32u, "time page copy ABI");
_Static_assert(sizeof(AstraTimePage) == 80u, "time page ABI");

typedef struct AstraTimeSample {
    uint64_t monotonic_ns;
    uint64_t wall_ns;
    int32_t  utc_offset;
    uint32_t zone;
    uint32_t wall_valid;
} AstraTimeSample;

/**
 * Read one consistent copy. Returns 1, or 0 when the page is absent or not
 * this ABI (the caller falls back to the clock syscalls).
 */
static inline int
astra_time_page_read(const volatile AstraTimePage *page,
                     AstraTimeSample *sample)
{
    uint32_t before;
    uint32_t after;
    AstraTimePageCopy copy;

    if (page == 0 || sample == 0 || page->magic != ASTRA_TIME_PAGE_MAGIC ||
        page->version != ASTRA_TIME_PAGE_VERSION)
        return 0;
    do {
        const volatile AstraTimePageCopy *current;

        before = __atomic_load_n(&page->sequence, __ATOMIC_ACQUIRE);
        current = &page->copy[before & 1u];
        copy.monotonic_high = current->monotonic_high;
        copy.monotonic_low = current->monotonic_low;
        copy.wall_high = current->wall_high;
        copy.wall_low = current->wall_low;
        copy.utc_offset = current->utc_offset;
        copy.zone = current->zone;
        copy.flags = current->flags;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        after = __atomic_load_n(&page->sequence, __ATOMIC_RELAXED);
    } while (before != after);
    sample->monotonic_ns = ((uint64_t)copy.monotonic_high << 32) |
                           copy.monotonic_low;
    sample->wall_ns = ((uint64_t)copy.wall_high << 32) | copy.wall_low;
    sample->utc_offset = copy.utc_offset;
    sample->zone = copy.zone;
    sample->wall_valid = (copy.flags & ASTRA_TIME_PAGE_WALL_VALID) != 0u;
    return 1;
}

/**
 * The writer half, shared by the kernel and the host test so both exercise
 * one implementation. Writes the copy readers are not using, then publishes.
 */
static inline void
astra_time_page_publish(volatile AstraTimePage *page,
                        const AstraTimePageCopy *value)
{
    uint32_t next = page->sequence + 1u;
    volatile AstraTimePageCopy *target = &page->copy[next & 1u];

    target->monotonic_high = value->monotonic_high;
    target->monotonic_low = value->monotonic_low;
    target->wall_high = value->wall_high;
    target->wall_low = value->wall_low;
    target->utc_offset = value->utc_offset;
    target->zone = value->zone;
    target->flags = value->flags;
    target->reserved = 0u;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&page->sequence, next, __ATOMIC_RELEASE);
}

#endif
```

- [ ] **Step 3: Write the failing host test**

`sw/userspace/runtime/tests/test_time_page.c`:

```c
#include <astra/time_page.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static AstraTimePage page;

static AstraTimePageCopy
value(uint64_t monotonic, uint64_t wall, int32_t offset)
{
    AstraTimePageCopy copy;

    memset(&copy, 0, sizeof(copy));
    copy.monotonic_high = (uint32_t)(monotonic >> 32);
    copy.monotonic_low = (uint32_t)monotonic;
    copy.wall_high = (uint32_t)(wall >> 32);
    copy.wall_low = (uint32_t)wall;
    copy.utc_offset = offset;
    copy.zone = 0x45445400u; /* "EDT\0" */
    copy.flags = ASTRA_TIME_PAGE_WALL_VALID;
    return copy;
}

int
main(void)
{
    AstraTimeSample sample;
    AstraTimePageCopy first = value(5000000000ull, 1759680000000000000ull,
                                    -14400);
    AstraTimePageCopy second = value(5005000000ull, 1759680000005000000ull,
                                     -14400);

    /* Absent page: no magic. */
    assert(!astra_time_page_read(&page, &sample));
    page.magic = ASTRA_TIME_PAGE_MAGIC;
    page.version = ASTRA_TIME_PAGE_VERSION + 1u;
    assert(!astra_time_page_read(&page, &sample));
    page.version = ASTRA_TIME_PAGE_VERSION;

    astra_time_page_publish(&page, &first);
    assert(page.sequence == 1u);
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 5000000000ull);
    assert(sample.wall_ns == 1759680000000000000ull);
    assert(sample.utc_offset == -14400 && sample.wall_valid);

    /* The publish writes the other copy: the copy a reader holds is intact. */
    astra_time_page_publish(&page, &second);
    assert(page.sequence == 2u);
    assert(page.copy[1].monotonic_low == first.monotonic_low);
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 5005000000ull);

    /* A 64-bit value whose low word wraps. */
    astra_time_page_publish(&page, &(AstraTimePageCopy){
        .monotonic_high = 1u, .monotonic_low = 0u });
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 0x100000000ull && !sample.wall_valid);

    puts("TIME PAGE PASS");
    return 0;
}
```

Add to `sw/userspace/runtime/Makefile`:

- the variable, next to `STRING_TEST`:

  ```make
  TIME_PAGE_TEST := build/host/test_time_page
  ```

- `$(TIME_PAGE_TEST)` to the `test:` prerequisites, and `./$(TIME_PAGE_TEST)`
  as the first line of its recipe;

- the rule:

  ```make
  $(TIME_PAGE_TEST): tests/test_time_page.c ../../include/astra/time_page.h
  	@mkdir -p $(@D)
  	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) $< -o $@
  ```

- [ ] **Step 4: Run it**

Run: `make -C sw/userspace/runtime build/host/test_time_page && sw/userspace/runtime/build/host/test_time_page`
Expected: `TIME PAGE PASS`. It passes immediately, because the header from step
2 is the implementation.

Perturb it to prove the test bites: in `astra_time_page_publish`, change
`&page->copy[next & 1u]` to `&page->copy[page->sequence & 1u]` and rerun. The
assertion `page.copy[1].monotonic_low == first.monotonic_low` must fail. Then
revert.

- [ ] **Step 5: `astra_time_now` in the runtime**

`sw/userspace/runtime/src/time_page.c`:

```c
/*
 * Time for callers that cannot afford a trap: the kernel time page, or the
 * clock syscalls when the page is absent (a kernel older than the page).
 */

#include <astra/runtime.h>
#include <astra/time_page.h>

void
astra_time_now(AstraTimeSample *sample)
{
    int32_t offset = 0;
    uint32_t zone = 0u;
    uint64_t wall = 0u;

    if (sample == NULL)
        return;
    if (astra_time_page_read(
            (const volatile AstraTimePage *)(uintptr_t)ASTRA_TIME_PAGE_ADDRESS,
            sample))
        return;
    sample->monotonic_ns = astra_clock_monotonic();
    sample->wall_valid = astra_clock_realtime_zone(&wall, &offset, &zone) ==
                         ASTRA_SYSCALL_OK;
    sample->wall_ns = sample->wall_valid ? wall : 0u;
    sample->utc_offset = sample->wall_valid ? offset : 0;
    sample->zone = sample->wall_valid ? zone : 0u;
}
```

Check the exact `astra_clock_realtime_zone` signature first:
`grep -n "astra_clock_realtime_zone" -A4 ndk/include/astra/runtime.h`. Adapt the
call to its parameter order. Then:

- Declare `void astra_time_now(AstraTimeSample *sample);` in
  `ndk/include/astra/runtime.h` beside `astra_clock_monotonic`, with
  `#include <astra/time_page.h>`.
- Add `src/time_page.c` to `TARGET_C_SOURCES`.
- Add `astra_time_now;` in sorted position to `runtime.library.exports`.

- [ ] **Step 6: Build and commit**

On beast:
`~/astra-mg/locked make -C sw/userspace/runtime -j8 && ~/astra-mg/locked make -C sw/userspace/runtime test && ~/astra-mg/locked make -C sw/kernel clean test`

Expected: all PASS, including `KERNEL ADDRESS SPACE PASS`.

```bash
git add sw/include/astra/time_page.h sw/include/astra/address_space.h \
  sw/userspace/runtime ndk/include/astra/runtime.h
git commit -m "feat(time): time page ABI, reader and astra_time_now"
```

---

### Task 2: Kernel publishes the time page

**Files:**
- Create: `sw/kernel/time_page.c`, `sw/kernel/time_page.h`
- Modify: `sw/kernel/vm.c` (`shared_mapping_class`, `kernel_vm_map_shared_page` admit mask)
- Modify: `sw/kernel/process.c` (launch, exec, tick, switch)
- Modify: `sw/kernel/Makefile` (source lists, tests)
- Test: `sw/kernel/tests/test_vm.c`, `sw/kernel/tests/test_process.c`

**Interfaces:**
- Consumes: `astra_time_page_publish`, `ASTRA_TIME_PAGE_*` (Task 1).
- Produces: `kernel_time_page_init(void) -> bool`,
  `kernel_time_page_update(void) -> void`,
  `kernel_time_page_map(KernelAddressSpace *space) -> KernelProcessStatus`.

- [ ] **Step 1: Failing VM test — a shared frame maps read-only into the time region in two spaces**

In `sw/kernel/tests/test_vm.c`, copy the existing test that calls
`kernel_vm_map_shared_page` into the dynamic-image region (search for it).
Change only the address and the expectations:

```c
static void
test_time_page_region_shared_mapping(void)
{
    /* Use the same space/frame helpers as the existing shared-page test in
     * this file: allocate one KERNEL_FRAME_SHARED frame owned by
     * TEST_TIME_OWNER, create two address spaces. */
    assert(kernel_vm_map_shared_page(&first, ASTRA_TIME_PAGE_ADDRESS_START,
                                     frame, TEST_TIME_OWNER,
                                     KERNEL_VM_READ) == KERNEL_VM_OK);
    assert(kernel_vm_map_shared_page(&second, ASTRA_TIME_PAGE_ADDRESS_START,
                                     frame, TEST_TIME_OWNER,
                                     KERNEL_VM_READ) == KERNEL_VM_OK);
    /* Read-only: a write mapping of the time region is refused. */
    assert(kernel_vm_map_shared_page(&third, ASTRA_TIME_PAGE_ADDRESS_START,
                                     frame, TEST_TIME_OWNER,
                                     KERNEL_VM_READ | KERNEL_VM_WRITE) !=
           KERNEL_VM_OK);
    /* Destroying one space leaves the frame mapped in the other. */
    kernel_vm_destroy_address_space(&first);
    assert(kernel_vm_translate(&second, ASTRA_TIME_PAGE_ADDRESS_START, ...)
           == KERNEL_VM_OK);
}
```

Use the file's actual helper names for allocation, translation and space
creation. The neighbouring shared-page test shows them. Register the function in
`main`.

Run on beast: `~/astra-mg/locked make -C sw/kernel clean build/test_vm && sw/kernel/build/test_vm`
Expected: FAIL with `KERNEL_VM_CACHE_ALIAS` or an assertion. The region is not
admitted yet.

- [ ] **Step 2: Admit the region**

In `sw/kernel/vm.c`:

1. Add the class next to the existing ones (`vm.c:53-58`):

   ```c
   #define VM_MAPPING_TIME_CLASS 0xfdu
   ```

2. Make `shared_mapping_class()` return `VM_MAPPING_TIME_CLASS` for addresses
   in `ASTRA_ADDRESS_REGION_TIME_PAGE`. Follow the existing dynamic and area
   branches.

3. In `kernel_vm_map_shared_page` (`vm.c:1786-1800`), add
   `(1u << ASTRA_ADDRESS_REGION_TIME_PAGE)` to the admit mask.

4. Refuse `KERNEL_VM_WRITE` when the address is in the time region. Return
   `KERNEL_VM_INVALID_ARGUMENT` before calling `map_owned_page`:

   ```c
   if (astra_address_region(virtual_address, KERNEL_PAGE_SIZE) ==
           ASTRA_ADDRESS_REGION_TIME_PAGE &&
       (permissions & KERNEL_VM_WRITE) != 0u)
       return KERNEL_VM_INVALID_ARGUMENT;
   ```

Run: `sw/kernel/build/test_vm` (rebuilt). Expected: PASS.

- [ ] **Step 3: `sw/kernel/time_page.{h,c}`**

`time_page.h`:

```c
#ifndef ASTRA_KERNEL_TIME_PAGE_H
#define ASTRA_KERNEL_TIME_PAGE_H

#include <stdbool.h>

#include "process.h"
#include "vm.h"

#define KERNEL_TIME_PAGE_OWNER 0x30000002u /* kernel shared-frame namespace; "TIME" collides with area owners */

bool kernel_time_page_init(void);
void kernel_time_page_update(void);
KernelProcessStatus kernel_time_page_map(KernelAddressSpace *space);

#endif
```

`time_page.c`:

```c
/*
 * One frame the kernel owns and every process maps read-only at
 * ASTRA_TIME_PAGE_ADDRESS. Every RAM mapping here is write-through and the
 * MC68040 caches are physically tagged (docs/MEMORY_MAP_AND_PMMU.md:133-149),
 * so a store through the identity alias is what the next user read sees: no
 * cache push is needed.
 */

#include "time_page.h"

#include <astra/time_page.h>

#include "allocation.h"
#include "bytes.h"
#include "memory.h"
#include "platform.h"

static uint32_t time_physical;
static volatile AstraTimePage *time_page;

bool
kernel_time_page_init(void)
{
    if (time_page != 0)
        return true;
    if (kernel_memory_alloc_zeroed_tagged(KERNEL_ALLOCATION_SITE_TIME_PAGE,
                                          1u, 1u, KERNEL_FRAME_SHARED,
                                          KERNEL_TIME_PAGE_OWNER,
                                          &time_physical) !=
        KERNEL_MEMORY_OK)
        return false;
    time_page = (volatile AstraTimePage *)kernel_memory_access(
        time_physical, KERNEL_PAGE_SIZE);
    time_page->version = ASTRA_TIME_PAGE_VERSION;
    kernel_time_page_update();
    /* Magic last: a reader never sees a page with magic and no time. */
    time_page->magic = ASTRA_TIME_PAGE_MAGIC;
    return true;
}

void
kernel_time_page_update(void)
{
    AstraTimePageCopy copy;
    uint64_t monotonic;
    uint64_t wall = 0u;
    int32_t offset = 0;
    uint32_t zone = 0u;

    if (time_page == 0)
        return;
    kernel_bytes_clear(&copy, sizeof(copy));
    monotonic = kernel_platform_monotonic_ns();
    copy.monotonic_high = (uint32_t)(monotonic >> 32);
    copy.monotonic_low = (uint32_t)monotonic;
    if (kernel_platform_wall_clock(&wall, &offset, &zone)) {
        copy.wall_high = (uint32_t)(wall >> 32);
        copy.wall_low = (uint32_t)wall;
        copy.utc_offset = offset;
        copy.zone = zone;
        copy.flags = ASTRA_TIME_PAGE_WALL_VALID;
    }
    astra_time_page_publish(time_page, &copy);
}

KernelProcessStatus
kernel_time_page_map(KernelAddressSpace *space)
{
    if (time_page == 0)
        return KERNEL_PROCESS_OK; /* no page: readers fall back */
    return kernel_vm_map_shared_page(space, ASTRA_TIME_PAGE_ADDRESS,
                                     time_physical, KERNEL_TIME_PAGE_OWNER,
                                     KERNEL_VM_READ) == KERNEL_VM_OK ?
        KERNEL_PROCESS_OK : KERNEL_PROCESS_RESOURCE_LIMIT;
}
```

Add `KERNEL_ALLOCATION_SITE_TIME_PAGE` to the allocation-site enum and name
table in `sw/kernel/allocation.{h,c}`, following `LIBRARY_METADATA`. Add
`time_page.c` to the kernel source list in `sw/kernel/Makefile`, and to the
link of every host test that links `process.c`. Find them with
`grep -n "process.c" sw/kernel/Makefile`.

- [ ] **Step 4: Failing process test — launch and exec map the page; the tick updates it**

In `sw/kernel/tests/test_process.c`, after an existing launch test sets up a
process (search for the first `publish_startup_block` coverage), add the
following. Call `kernel_time_page_init()` once in the test's setup.

```c
static void
test_time_page_mapped_and_updated(void)
{
    AstraTimePage observed;
    uint32_t before;

    /* launch a process as the neighbouring tests do */
    assert(kernel_vm_read(&process->address_space, ASTRA_TIME_PAGE_ADDRESS,
                          &observed, sizeof(observed)) == KERNEL_VM_OK);
    assert(observed.magic == ASTRA_TIME_PAGE_MAGIC);
    before = observed.sequence;
    kernel_time_page_update();
    assert(kernel_vm_read(&process->address_space, ASTRA_TIME_PAGE_ADDRESS,
                          &observed, sizeof(observed)) == KERNEL_VM_OK);
    assert(observed.sequence == before + 1u);
    /* After exec_replace the new space maps it too: reuse the existing exec
     * test's driver, then repeat the first read. */
}
```

Expected: FAIL (`KERNEL_VM_NOT_MAPPED`).

- [ ] **Step 5: Map at launch and exec, update on tick and switch**

In `sw/kernel/process.c`:

- **Launch.** Find the launch path that calls `publish_startup_block` for a new
  process: `grep -n "publish_startup_block(" sw/kernel/process.c`; the call at
  ~7120 is the definition. Right after a successful call there, add the
  following, following that function's existing error unwinding:

  ```c
  status = kernel_time_page_map(space);
  if (status != KERNEL_PROCESS_OK)
      goto failed;
  ```

- **Exec.** In `exec_replace`, after `publish_startup_block(replacement, ...)`
  (~4850) and before the "no-allocation commit" comment (~4876):

  ```c
  status = kernel_time_page_map(replacement);
  if (status != KERNEL_PROCESS_OK)
      goto failed;
  ```

- **Tick.** In `kernel_process_on_timer` (~8496), first statement:
  `kernel_time_page_update();`. In `kernel_process_on_supervisor_timer`
  (~8562), first statement: `kernel_time_page_update();`.

- **Switch.** In `activate_fast` (~1394), inside `if (previous != next)` after
  `kernel_vm_switch` succeeds: `kernel_time_page_update();`.

- **Boot.** Call `kernel_time_page_init()` from kernel initialization after the
  memory allocator and platform clocks are up and before the first process
  launch. Use the same place that initializes the library cache metadata:
  `grep -n "LIBRARY_CACHE_OWNER" sw/kernel/process.c`. If it fails, panic with
  the existing panic helper. A kernel that cannot allocate one page at boot
  cannot launch anything either.

- **Fork.** No change: `kernel_vm_clone_address_space` re-maps shared frames
  (`vm.c:2166-2175`). Add one assertion to the existing fork test that the
  child reads `ASTRA_TIME_PAGE_MAGIC` at `ASTRA_TIME_PAGE_ADDRESS`.

Run on beast: `~/astra-mg/locked make -C sw/kernel clean test`. Expected: all
PASS.

- [ ] **Step 6: Float-free and size check, m68k build**

On beast:
`~/astra-mg/locked make -C sw/kernel clean && ~/astra-mg/locked make -C sw/userspace -j8 && ~/astra-mg/locked make -C sw/boot`

Expected: the build succeeds and `tools/check_kernel_float_free.py` passes as
part of the link. Then `make -C sw/kernel clean`.

- [ ] **Step 7: Boot check**

Boot the base image under QEMU with the terminal gate:
`python3 emu/qemu/test-terminal.py <qemu> sw/boot/build/astra_boot.bin --image ~/astra-mg/term.img`

Expected: PASS. Every process now maps the page, so a mapping bug surfaces here
as launch failures.

- [ ] **Step 8: Commit**

```bash
git add sw/kernel sw/include/astra
git commit -m "feat(kernel): publish the time page in every process"
```

---

### Task 3: Log ring ABI and the lock-free producer/consumer core

**Files:**
- Create: `sw/include/astra/log_ring.h`
- Create: `sw/userspace/runtime/src/log_ring.c`
- Create: `sw/userspace/runtime/tests/test_log_ring.c`
- Modify: `sw/userspace/runtime/Makefile`, `runtime.library.exports`

**Interfaces:**
- Consumes: `AstraBulkRingHeader` (`sw/include/astra/object_abi.h:87-114`).
- Produces:

  ```c
  void astra_log_producer_init(AstraLogProducer *p, volatile void *ring_base,
                               AstraLogNotify notify, void *context);
  uint32_t astra_log_producer_write(AstraLogProducer *p,
                                    const AstraLogRecordFields *fields,
                                    const char *text, uint32_t length);
      /* ASTRA_LOG_WRITE_OK | ASTRA_LOG_WRITE_DROPPED | ASTRA_LOG_WRITE_DEAD */
  uint32_t astra_log_consumer_next(volatile const void *ring_base,
                                   uint32_t *position,
                                   AstraLogRecordFields *fields, char *text,
                                   uint32_t capacity, uint32_t *length);
      /* ASTRA_LOG_READ_OK | ASTRA_LOG_READ_EMPTY | ASTRA_LOG_READ_CORRUPT */
  ```

- [ ] **Step 1: Write `sw/include/astra/log_ring.h`**

```c
#ifndef ASTRA_LOG_RING_H
#define ASTRA_LOG_RING_H

/** @file log_ring.h
 * @brief One process's log ring: a bulk ring of fixed slots in a shared area.
 *
 * docs/SYSTEM_LOG.md section 3. The process is the producer, the events
 * service the consumer. Writers inside the process reserve slots with one CAS
 * on a process-local counter, fill them, set each slot's commit word to its
 * position + 1, and advance the shared producer position over committed slots
 * with CAS. No writer waits for another.
 */

#include <stdint.h>

#include <astra/object_abi.h>

#define ASTRA_LOG_RING_AREA_SIZE    65536u
#define ASTRA_LOG_RING_OFFSET       0u
#define ASTRA_LOG_RING_SLOT_SIZE    252u
#define ASTRA_LOG_RING_CAPACITY     256u
#define ASTRA_LOG_RING_HEADER_SIZE  64u
#define ASTRA_LOG_SLOT_PAYLOAD      (ASTRA_LOG_RING_SLOT_SIZE - 4u)   /* 248 */
#define ASTRA_LOG_RECORD_HEADER_SIZE 40u
#define ASTRA_LOG_FIRST_TEXT        (ASTRA_LOG_SLOT_PAYLOAD - \
                                     ASTRA_LOG_RECORD_HEADER_SIZE)    /* 208 */
#define ASTRA_LOG_RECORD_SLOTS_MAX  8u
#define ASTRA_LOG_TEXT_MAX          (ASTRA_LOG_FIRST_TEXT + \
                                     (ASTRA_LOG_RECORD_SLOTS_MAX - 1u) * \
                                     ASTRA_LOG_SLOT_PAYLOAD)          /* 1944 */

_Static_assert(ASTRA_LOG_RING_HEADER_SIZE +
               ASTRA_LOG_RING_CAPACITY * ASTRA_LOG_RING_SLOT_SIZE <=
               ASTRA_LOG_RING_AREA_SIZE, "log ring fits its area");

#define ASTRA_LOG_STREAM_LOG    0u
#define ASTRA_LOG_STREAM_STDOUT 1u
#define ASTRA_LOG_STREAM_STDERR 2u
#define ASTRA_LOG_STREAM_KERNEL 3u
#define ASTRA_LOG_STREAM_MAX    3u

#define ASTRA_LOG_FLAG_TRUNCATED 0x1u
#define ASTRA_LOG_FLAG_WALL_VALID 0x2u

/** Decoded record header. Stored in the first slot, native byte order. */
typedef struct AstraLogRecordFields {
    uint32_t sequence;
    uint32_t monotonic_high;
    uint32_t monotonic_low;
    uint32_t wall_high;
    uint32_t wall_low;
    int32_t  utc_offset;
    uint32_t zone;
    uint32_t dropped;        /* lines this process dropped, ever */
    uint16_t text_length;
    uint8_t  level;          /* ASTRA_EVENT_LEVEL_* */
    uint8_t  stream;         /* ASTRA_LOG_STREAM_* */
    uint8_t  slots;          /* 1..ASTRA_LOG_RECORD_SLOTS_MAX */
    uint8_t  flags;          /* ASTRA_LOG_FLAG_* */
    uint16_t reserved;
} AstraLogRecordFields;

_Static_assert(sizeof(AstraLogRecordFields) == ASTRA_LOG_RECORD_HEADER_SIZE,
               "log record header ABI");

/* The setup message a process sends the LOG port with [consumer, area]. */
#define ASTRA_CAPABILITY_LOG       "LOG"
#define ASTRA_LOG_SETUP_MAGIC      0x414c4f47u /* "ALOG" */
#define ASTRA_LOG_SETUP_VERSION    1u
#define ASTRA_LOG_IDENT_MAX        32u

typedef struct AstraLogSetup {
    uint32_t magic;
    uint32_t version;
    uint32_t area_size;
    uint32_t ring_offset;
    uint32_t slot_size;
    uint32_t capacity;
    char     ident[ASTRA_LOG_IDENT_MAX];   /* NUL-terminated */
} AstraLogSetup;

/* The doorbell: returns nonzero when the service is gone (PEER_DEAD). */
typedef uint32_t (*AstraLogNotify)(void *context);
#define ASTRA_LOG_DOORBELL_MAGIC   0x414c4442u /* "ALDB", 4-byte message */

/** Process-local producer state. Every field is written only by CAS or by
 * the setup path before `ready` is published. */
typedef struct AstraLogProducer {
    volatile AstraBulkRingHeader *header;
    volatile uint8_t *slots;
    AstraLogNotify notify;
    void *context;
    uint32_t reserved;     /* next position to hand out */
    uint32_t sequence;
    uint32_t dropped;
    uint32_t dead;         /* nonzero once the service is gone */
} AstraLogProducer;

#define ASTRA_LOG_WRITE_OK      0u
#define ASTRA_LOG_WRITE_DROPPED 1u
#define ASTRA_LOG_WRITE_DEAD    2u

#define ASTRA_LOG_READ_OK       0u
#define ASTRA_LOG_READ_EMPTY    1u
#define ASTRA_LOG_READ_CORRUPT  2u

void astra_log_producer_init(AstraLogProducer *producer,
                             volatile void *ring_base, AstraLogNotify notify,
                             void *context);
uint32_t astra_log_record_slots(uint32_t text_length);
uint32_t astra_log_producer_write(AstraLogProducer *producer,
                                  const AstraLogRecordFields *fields,
                                  const char *text, uint32_t length);
uint32_t astra_log_consumer_next(volatile const void *ring_base,
                                 uint32_t *position,
                                 AstraLogRecordFields *fields, char *text,
                                 uint32_t capacity, uint32_t *length);

#endif
```

- [ ] **Step 2: Write the failing test**

`sw/userspace/runtime/tests/test_log_ring.c`:

```c
#include <astra/log_ring.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t area[ASTRA_LOG_RING_AREA_SIZE];
static uint32_t notifications[16];
static uint32_t notification_count;

static uint32_t
record_notify(void *context)
{
    (void)context;
    notifications[notification_count % 16u] =
        ((AstraBulkRingHeader *)(void *)area)->producer_position;
    ++notification_count;
    return 0u; /* the service is alive */
}

static void
format_ring(void)
{
    volatile AstraBulkRingHeader *header = (void *)area;

    memset(area, 0, sizeof(area));
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->element_size = ASTRA_LOG_RING_SLOT_SIZE;
    header->capacity = ASTRA_LOG_RING_CAPACITY;
    header->data_offset = ASTRA_LOG_RING_HEADER_SIZE;
    notification_count = 0u;
}

static AstraLogRecordFields
fields(uint8_t level, uint8_t stream)
{
    AstraLogRecordFields value;

    memset(&value, 0, sizeof(value));
    value.level = level;
    value.stream = stream;
    value.monotonic_low = 42u;
    return value;
}

static void
drain_expect(uint32_t *position, const char *expected, uint32_t dropped)
{
    AstraLogRecordFields read;
    char text[ASTRA_LOG_TEXT_MAX];
    uint32_t length = 0u;

    assert(astra_log_consumer_next(area, position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_OK);
    assert(length == strlen(expected));
    assert(memcmp(text, expected, length) == 0);
    assert(read.dropped == dropped);
    assert(read.monotonic_low == 42u);
}

static void
test_slots(void)
{
    assert(astra_log_record_slots(0u) == 1u);
    assert(astra_log_record_slots(208u) == 1u);
    assert(astra_log_record_slots(209u) == 2u);
    assert(astra_log_record_slots(208u + 248u) == 2u);
    assert(astra_log_record_slots(ASTRA_LOG_TEXT_MAX) == 8u);
    assert(astra_log_record_slots(ASTRA_LOG_TEXT_MAX + 1u) == 8u);
}

static void
test_round_trip_and_notify_once_per_burst(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_STDOUT);
    uint32_t position = 0u;
    volatile AstraBulkRingHeader *header = (void *)area;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    assert(astra_log_producer_write(&producer, &value, "first", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(header->producer_position == 1u);
    assert(notification_count == 1u && notifications[0] == 1u);
    /* Consumer has not moved: the second line in the burst does not notify. */
    assert(astra_log_producer_write(&producer, &value, "second", 6u) ==
           ASTRA_LOG_WRITE_OK);
    assert(notification_count == 1u);
    drain_expect(&position, "first", 0u);
    drain_expect(&position, "second", 0u);
    header->consumer_position = position;
    /* The consumer caught up to what the kernel was told: notify again. */
    assert(astra_log_producer_write(&producer, &value, "third", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(notification_count == 2u);
}

static void
test_long_record_wraps_the_ring(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(4u, ASTRA_LOG_STREAM_STDERR);
    char text[1000];
    uint32_t position = 0u;
    volatile AstraBulkRingHeader *header = (void *)area;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    /* Fill to two slots before the end, so a 5-slot record wraps. */
    for (uint32_t index = 0u; index < ASTRA_LOG_RING_CAPACITY - 2u; ++index) {
        assert(astra_log_producer_write(&producer, &value, "x", 1u) ==
               ASTRA_LOG_WRITE_OK);
        drain_expect(&position, "x", 0u);
        header->consumer_position = position;
    }
    for (uint32_t index = 0u; index < sizeof(text); ++index)
        text[index] = (char)('a' + index % 26u);
    assert(astra_log_producer_write(&producer, &value, text, sizeof(text)) ==
           ASTRA_LOG_WRITE_OK);
    {
        AstraLogRecordFields read;
        char out[ASTRA_LOG_TEXT_MAX];
        uint32_t length = 0u;

        assert(astra_log_consumer_next(area, &position, &read, out,
                                       sizeof(out), &length) ==
               ASTRA_LOG_READ_OK);
        assert(length == sizeof(text) && memcmp(out, text, length) == 0);
        assert(read.slots == astra_log_record_slots(sizeof(text)));
    }
}

static void
test_full_ring_drops_and_counts(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    uint32_t position = 0u;
    uint32_t written = 0u;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    while (astra_log_producer_write(&producer, &value, "line", 4u) ==
           ASTRA_LOG_WRITE_OK)
        ++written;
    assert(written == ASTRA_LOG_RING_CAPACITY);
    assert(astra_log_producer_write(&producer, &value, "line", 4u) ==
           ASTRA_LOG_WRITE_DROPPED);
    assert(producer.dropped == 2u);
    for (uint32_t index = 0u; index < written; ++index)
        drain_expect(&position, "line", 0u);
    ((volatile AstraBulkRingHeader *)area)->consumer_position = position;
    assert(astra_log_producer_write(&producer, &value, "after", 5u) ==
           ASTRA_LOG_WRITE_OK);
    drain_expect(&position, "after", 2u);
}

static void
test_uncommitted_slot_holds_back_publication(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    volatile AstraBulkRingHeader *header = (void *)area;
    uint32_t position = 0u;
    AstraLogRecordFields read;
    char text[64];
    uint32_t length;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    /* Simulate a writer preempted after reserving slot 0. */
    producer.reserved = 1u;
    assert(astra_log_producer_write(&producer, &value, "later", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(header->producer_position == 0u); /* slot 0 not committed */
    assert(astra_log_consumer_next(area, &position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_EMPTY);
}

static void
test_corrupt_slot_count_is_rejected(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    uint32_t position = 0u;
    AstraLogRecordFields read;
    char text[64];
    uint32_t length;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    assert(astra_log_producer_write(&producer, &value, "ok", 2u) ==
           ASTRA_LOG_WRITE_OK);
    /* Claim 9 slots in the first slot's header: more than published. */
    area[ASTRA_LOG_RING_HEADER_SIZE + 4u +
         __builtin_offsetof(AstraLogRecordFields, slots)] = 9u;
    assert(astra_log_consumer_next(area, &position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_CORRUPT);
}

int
main(void)
{
    test_slots();
    test_round_trip_and_notify_once_per_burst();
    test_long_record_wraps_the_ring();
    test_full_ring_drops_and_counts();
    test_uncommitted_slot_holds_back_publication();
    test_corrupt_slot_count_is_rejected();
    puts("LOG RING PASS");
    return 0;
}
```

Check `ASTRA_BULK_RING_MAGIC` and the header field names against
`sw/include/astra/object_abi.h:87-114`, and use the real names.

Makefile:

- the variable:

  ```make
  LOG_RING_TEST := build/host/test_log_ring
  ```

- `$(LOG_RING_TEST)` added to `test:`, and `./$(LOG_RING_TEST)` to its recipe;

- the rule:

  ```make
  $(LOG_RING_TEST): src/log_ring.c tests/test_log_ring.c \
  		../../include/astra/log_ring.h
  	@mkdir -p $(@D)
  	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
  		src/log_ring.c tests/test_log_ring.c -o $@
  ```

Run: `make -C sw/userspace/runtime build/host/test_log_ring`
Expected: link failure (undefined `astra_log_producer_init`, etc.).

- [ ] **Step 3: Implement `sw/userspace/runtime/src/log_ring.c`**

```c
/*
 * The log ring's two halves. Pure: no syscalls, so the host test drives both
 * against one buffer. docs/SYSTEM_LOG.md section 3 is the design.
 *
 * Slot layout: [commit word][248 payload]. The first slot's payload is the
 * 40-byte AstraLogRecordFields and 208 bytes of text; continuation slots are
 * text. A slot's commit word is its ring position + 1 once its bytes are
 * written: a value from an earlier lap differs by a multiple of the capacity
 * and never matches, so the words need no clearing.
 */

#include <astra/log_ring.h>

#include <stddef.h>

static inline int
cas32(uint32_t *word, uint32_t expected, uint32_t desired)
{
    /* Explicit CAS, never __atomic_exchange_n: runtime.h:785-800. */
    return __atomic_compare_exchange_n(word, &expected, desired, 0,
                                       __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static inline uint32_t
load32(const volatile uint32_t *word)
{
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

static volatile uint8_t *
slot_at(volatile uint8_t *slots, uint32_t position)
{
    return slots + (position & (ASTRA_LOG_RING_CAPACITY - 1u)) *
                   ASTRA_LOG_RING_SLOT_SIZE;
}

static volatile uint32_t *
commit_word(volatile uint8_t *slot)
{
    return (volatile uint32_t *)(void *)slot;
}

uint32_t
astra_log_record_slots(uint32_t text_length)
{
    uint32_t rest;

    if (text_length > ASTRA_LOG_TEXT_MAX)
        text_length = ASTRA_LOG_TEXT_MAX;
    if (text_length <= ASTRA_LOG_FIRST_TEXT)
        return 1u;
    rest = text_length - ASTRA_LOG_FIRST_TEXT;
    return 1u + (rest + ASTRA_LOG_SLOT_PAYLOAD - 1u) / ASTRA_LOG_SLOT_PAYLOAD;
}

void
astra_log_producer_init(AstraLogProducer *producer, volatile void *ring_base,
                        AstraLogNotify notify, void *context)
{
    volatile uint8_t *base = ring_base;

    producer->header = (volatile AstraBulkRingHeader *)(void *)base;
    producer->slots = base + ASTRA_LOG_RING_HEADER_SIZE;
    producer->notify = notify;
    producer->context = context;
    producer->reserved = producer->header->producer_position;
    producer->sequence = 0u;
    producer->dropped = 0u;
    producer->dead = 0u;
}

static void
copy_out(volatile uint8_t *to, const void *from, uint32_t length)
{
    const uint8_t *bytes = from;

    for (uint32_t index = 0u; index < length; ++index)
        to[index] = bytes[index];
}

/* Advance producer_position over every committed slot. Any writer may do it;
 * CAS keeps it monotonic when two do it at once. */
static void
publish(AstraLogProducer *producer)
{
    uint32_t *position =
        (uint32_t *)(uintptr_t)&producer->header->producer_position;

    for (;;) {
        uint32_t at = __atomic_load_n(position, __ATOMIC_ACQUIRE);

        if (at == __atomic_load_n(&producer->reserved, __ATOMIC_ACQUIRE) ||
            load32(commit_word(slot_at(producer->slots, at))) != at + 1u)
            return;
        (void)cas32(position, at, at + 1u);
    }
}

/*
 * One doorbell per burst: ring only when this record begins where the
 * consumer stopped, i.e. the ring was empty as far as the service knew. While
 * the service is not running its position does not move, so the rest of a
 * burst rings nothing. Concurrent rings are harmless (a full LOG queue already
 * means a wake is pending). The kernel ring object is never notified: the
 * consumer reads published slots from the header directly, and a consumer
 * position reported past the kernel's producer shadow fails the ring.
 */
static void
maybe_notify(AstraLogProducer *producer, uint32_t first)
{
    if (producer->notify == NULL ||
        load32(&producer->header->consumer_position) != first)
        return;
    if (producer->notify(producer->context) != 0u)
        __atomic_store_n(&producer->dead, 1u, __ATOMIC_RELEASE);
}

uint32_t
astra_log_producer_write(AstraLogProducer *producer,
                         const AstraLogRecordFields *fields,
                         const char *text, uint32_t length)
{
    AstraLogRecordFields header;
    uint32_t slots;
    uint32_t first;
    uint32_t written = 0u;

    if (__atomic_load_n(&producer->dead, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_LOG_WRITE_DEAD;
    header = *fields;
    header.flags = (uint8_t)(header.flags & ASTRA_LOG_FLAG_WALL_VALID);
    if (length > ASTRA_LOG_TEXT_MAX) {
        length = ASTRA_LOG_TEXT_MAX;
        header.flags |= ASTRA_LOG_FLAG_TRUNCATED;
    }
    slots = astra_log_record_slots(length);
    for (;;) {
        uint32_t consumer = load32(&producer->header->consumer_position);

        first = __atomic_load_n(&producer->reserved, __ATOMIC_ACQUIRE);
        if (first + slots - consumer > ASTRA_LOG_RING_CAPACITY) {
            uint32_t seen = __atomic_load_n(&producer->dropped,
                                            __ATOMIC_RELAXED);

            while (!cas32(&producer->dropped, seen, seen + 1u))
                seen = __atomic_load_n(&producer->dropped, __ATOMIC_RELAXED);
            return ASTRA_LOG_WRITE_DROPPED;
        }
        if (cas32(&producer->reserved, first, first + slots))
            break;
    }
    {
        uint32_t sequence = __atomic_load_n(&producer->sequence,
                                            __ATOMIC_RELAXED);

        while (!cas32(&producer->sequence, sequence, sequence + 1u))
            sequence = __atomic_load_n(&producer->sequence, __ATOMIC_RELAXED);
        header.sequence = sequence;
    }
    header.dropped = __atomic_load_n(&producer->dropped, __ATOMIC_RELAXED);
    header.text_length = (uint16_t)length;
    header.slots = (uint8_t)slots;
    header.reserved = 0u;
    for (uint32_t index = 0u; index < slots; ++index) {
        volatile uint8_t *payload = slot_at(producer->slots, first + index) +
                                    4u;
        uint32_t room = ASTRA_LOG_SLOT_PAYLOAD;
        uint32_t chunk;

        if (index == 0u) {
            copy_out(payload, &header, sizeof(header));
            payload += sizeof(header);
            room = ASTRA_LOG_FIRST_TEXT;
        }
        chunk = length - written < room ? length - written : room;
        copy_out(payload, text + written, chunk);
        written += chunk;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* Continuation slots first, so the first slot's commit means all of it. */
    for (uint32_t index = slots; index-- != 0u;)
        __atomic_store_n(
            (uint32_t *)(uintptr_t)commit_word(
                slot_at(producer->slots, first + index)),
            first + index + 1u, __ATOMIC_RELEASE);
    publish(producer);
    maybe_notify(producer, first);
    return ASTRA_LOG_WRITE_OK;
}

static void
copy_in(void *to, const volatile uint8_t *from, uint32_t length)
{
    uint8_t *bytes = to;

    for (uint32_t index = 0u; index < length; ++index)
        bytes[index] = from[index];
}

uint32_t
astra_log_consumer_next(volatile const void *ring_base, uint32_t *position,
                        AstraLogRecordFields *fields, char *text,
                        uint32_t capacity, uint32_t *length)
{
    volatile const AstraBulkRingHeader *header = ring_base;
    volatile uint8_t *slots =
        (volatile uint8_t *)(uintptr_t)ring_base + ASTRA_LOG_RING_HEADER_SIZE;
    uint32_t produced = __atomic_load_n(&header->producer_position,
                                        __ATOMIC_ACQUIRE);
    uint32_t at = *position;
    uint32_t copied = 0u;

    *length = 0u;
    if (produced - at > ASTRA_LOG_RING_CAPACITY)
        return ASTRA_LOG_READ_CORRUPT;
    if (produced == at)
        return ASTRA_LOG_READ_EMPTY;
    copy_in(fields, slot_at(slots, at) + 4u, sizeof(*fields));
    if (fields->slots == 0u || fields->slots > ASTRA_LOG_RECORD_SLOTS_MAX ||
        fields->slots > produced - at ||
        fields->text_length > ASTRA_LOG_TEXT_MAX ||
        astra_log_record_slots(fields->text_length) != fields->slots ||
        fields->stream > ASTRA_LOG_STREAM_MAX || fields->level > 4u)
        return ASTRA_LOG_READ_CORRUPT;
    for (uint32_t index = 0u; index < fields->slots; ++index) {
        const volatile uint8_t *payload = slot_at(slots, at + index) + 4u;
        uint32_t room = ASTRA_LOG_SLOT_PAYLOAD;
        uint32_t chunk;

        if (index == 0u) {
            payload += sizeof(*fields);
            room = ASTRA_LOG_FIRST_TEXT;
        }
        chunk = fields->text_length - copied < room ?
            fields->text_length - copied : room;
        if (copied + chunk > capacity)
            chunk = capacity > copied ? capacity - copied : 0u;
        copy_in(text + copied, payload, chunk);
        copied += chunk;
    }
    *length = copied;
    *position = at + fields->slots;
    return ASTRA_LOG_READ_OK;
}
```

Run: `make -C sw/userspace/runtime build/host/test_log_ring && sw/userspace/runtime/build/host/test_log_ring`
Expected: `LOG RING PASS`.

Perturb each of these, see the failure the test names, and revert:

- Remove the `!= first` test in `maybe_notify`. The notification count fails.
- Commit slot 0 first instead of last. Not caught by a single-threaded test.
  Note this in the commit message: it is covered by the four-thread gate in
  Task 10.
- Drop `fields->slots > produced - at`. `test_corrupt_slot_count_is_rejected`
  fails.

- [ ] **Step 4: Add to the m68k build and exports**

- Add `src/log_ring.c` to `TARGET_C_SOURCES`.
- Add `astra_log_consumer_next`, `astra_log_producer_init`,
  `astra_log_producer_write` and `astra_log_record_slots` to
  `runtime.library.exports`, in sorted order.

On beast: `~/astra-mg/locked make -C sw/userspace/runtime -j8 test`.
Expected: build OK and all runtime tests pass, including
`check_shared_symbol_ownership.py`.

- [ ] **Step 5: Commit**

```bash
git add sw/include/astra/log_ring.h sw/userspace/runtime
git commit -m "feat(runtime): lock-free log ring producer and consumer"
```

---

### Task 4: `astra_log_line` — lazy ring setup, fallback, existing API routed

**Files:**
- Modify: `sw/userspace/runtime/src/log.c`
- Modify: `ndk/include/astra/runtime.h` (declaration)
- Modify: `sw/userspace/runtime/runtime.library.exports`
- Test: `sw/userspace/runtime/tests/test_runtime.c` (existing harness, which stubs `astra_syscall5`)

**Interfaces:**
- Consumes: Task 1 `astra_time_now`, Task 3 producer, `astra_rt_area_create_flagged`, `astra_rt_area_map`, `astra_rt_ring_create`, `astra_rt_handle_duplicate`, `astra_port_send`, `astra_startup_capability`, `astra_startup_argument`.
- Produces: `uint32_t astra_log_line(uint32_t level, uint32_t stream, const char *text, uint32_t length);`. It returns `ASTRA_SYSCALL_OK`, or `ASTRA_SYSCALL_INVALID_ARGUMENT` for a bad level, a bad stream or NULL text.

- [ ] **Step 1: Read the harness**

Run `grep -n "astra_syscall5\|LOG_WRITE" sw/userspace/runtime/tests/test_runtime.c | head`
to see how the existing test fakes syscalls and records `LOG_WRITE` calls. The
new test cases use that fake. Extend it to answer:

- `ASTRA_SYSCALL_AREA_CREATE`, `AREA_MAP` (returning the address of a static
  64 KiB host buffer), `RING_CREATE` (formatting the bulk-ring header in that
  buffer as the kernel does), `HANDLE_DUPLICATE` and `PORT_SEND_TRY`, counting
  4-byte doorbell sends separately from setup sends;
- with switchable results, so each failure path can be driven.

The startup block is at the fixed address `ASTRA_STARTUP_ADDRESS_START`, which a
host test cannot map. Make the address a link-time indirection:

```c
/* log.c */
#ifndef ASTRA_LOG_STARTUP_ADDRESS
#define ASTRA_LOG_STARTUP_ADDRESS ((const AstraStartupInfo *)(uintptr_t) \
                                   ASTRA_STARTUP_ADDRESS_START)
#endif
```

The host test compiles `log.c` with
`-DASTRA_LOG_STARTUP_ADDRESS=astra_test_startup()` and provides a startup block
carrying a `LOG` capability and argv `{"/apps/ChocolateDoom.app/chocolate-doom"}`.
The time page is not mapped on the host, so compile with
`-DASTRA_TIME_PAGE_ADDRESS=astra_test_time_page_address()` as well. Make that
macro overridable in `time_page.h` with `#ifndef`.

- [ ] **Step 2: Write the failing tests** (add to `test_runtime.c`)

```c
static void
test_log_line_sets_up_ring_once_and_writes_records(void)
{
    reset_fake_kernel();
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_STDOUT,
                          "V_Init: allocate screens.\n", 26u) ==
           ASTRA_SYSCALL_OK);
    assert(fake.area_creates == 1u && fake.ring_creates == 1u);
    assert(fake.port_sends == 1u && fake.doorbells == 1u);
    assert(fake.last_send_handle_count == 2u);
    assert(strcmp(fake.last_setup.ident, "chocolate-doom") == 0);
    assert(fake.last_setup.magic == ASTRA_LOG_SETUP_MAGIC);
    assert(fake.log_writes == 0u); /* not the kernel ring */
    {
        AstraLogRecordFields read;
        char text[64];
        uint32_t length;
        uint32_t position = 0u;

        assert(astra_log_consumer_next(fake.area, &position, &read, text,
                                       sizeof(text), &length) ==
               ASTRA_LOG_READ_OK);
        /* The trailing newline is not part of the record. */
        assert(length == 25u && memcmp(text, "V_Init: allocate screens.", 25u)
               == 0);
        assert(read.level == ASTRA_EVENT_LEVEL_NOTICE &&
               read.stream == ASTRA_LOG_STREAM_STDOUT);
    }
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_STDOUT,
                          "two", 3u) == ASTRA_SYSCALL_OK);
    assert(fake.area_creates == 1u && fake.port_sends == 1u);
    assert(fake.doorbells == 1u); /* the service has not drained: no ring */
    fake.doorbell_result = ASTRA_SYSCALL_PEER_DEAD;
    fake.consume_all(); /* consumer catches up, so the next line rings */
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG,
                          "gone", 4u) == ASTRA_SYSCALL_OK);
    /* the service died: that line was in the ring; the next one falls back */
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG,
                          "after", 5u) == ASTRA_SYSCALL_OK);
    assert(fake.log_writes == 1u);
}

static void
test_log_line_falls_back_when_send_refused_then_retries(void)
{
    reset_fake_kernel();
    fake.port_send_result = ASTRA_SYSCALL_WOULD_BLOCK;
    assert(astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG,
                          "early", 5u) == ASTRA_SYSCALL_OK);
    assert(fake.log_writes == 1u);       /* went to LOG_WRITE */
    assert(fake.closed_handles >= 3u);   /* ring endpoints, area undone */
    fake.port_send_result = ASTRA_SYSCALL_OK;
    for (uint32_t index = 0u; index < 63u; ++index)
        (void)astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG,
                             "x", 1u);
    assert(fake.area_creates == 1u);     /* still backing off */
    (void)astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG,
                         "x", 1u);
    assert(fake.area_creates == 2u && fake.port_sends == 2u);
}

static void
test_log_line_without_log_capability_uses_kernel_ring(void)
{
    reset_fake_kernel();
    fake.startup_has_log = 0;
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG,
                          "boot", 4u) == ASTRA_SYSCALL_OK);
    assert(fake.area_creates == 0u && fake.log_writes == 1u);
}

static void
test_log_line_debug_never_enters_the_ring(void)
{
    reset_fake_kernel();
    assert(astra_log_line(ASTRA_EVENT_LEVEL_DEBUG, ASTRA_LOG_STREAM_LOG,
                          "echo", 4u) == ASTRA_SYSCALL_OK);
    assert(fake.area_creates == 0u && fake.log_writes == 1u);
}

static void
test_log_line_records_empty_lines(void)
{
    reset_fake_kernel();
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_STDOUT,
                          "\n", 1u) == ASTRA_SYSCALL_OK);
    assert(astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_STDOUT,
                          "", 0u) == ASTRA_SYSCALL_OK);
    assert(fake.ring_records == 2u && last_ring_text_equals(""));
}

static void
test_log_line_rejects_bad_arguments(void)
{
    assert(astra_log_line(5u, ASTRA_LOG_STREAM_LOG, "x", 1u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log_line(2u, 9u, "x", 1u) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log_line(2u, 0u, NULL, 1u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
}

static void
test_existing_astra_log_routes_through_ring(void)
{
    reset_fake_kernel();
    assert(astra_log("volume mounted at /") == ASTRA_SYSCALL_OK);
    assert(fake.ring_records == 1u && fake.log_writes == 0u);
    assert(astra_log_failure("media host", 12u) == ASTRA_SYSCALL_OK);
    assert(last_ring_text_equals("media host:12: failed"));
}
```

Give the fake a `last_ring_text_equals` helper that decodes the newest record
with `astra_log_consumer_next`. Add the test file's `log.c`/`log_ring.c`/
`time_page.c` sources and the two `-D` overrides to the `$(HOST_TEST)` rule.

Run: `make -C sw/userspace/runtime build/host/test_runtime`. Expected: FAIL
(undefined `astra_log_line`).

- [ ] **Step 3: Implement in `sw/userspace/runtime/src/log.c`**

Add the following after the includes, adding `#include <astra/log_ring.h>`,
`#include <astra/time_page.h>`, `#include <astra/process.h>` and
`#include <astra/address_space.h>`:

```c
/*
 * The process's log ring, made on the first line that wants it. Setup is
 * claimed with one CAS; a thread that logs while another is setting up uses
 * LOG_WRITE for that line rather than wait (docs/SYSTEM_LOG.md section 3).
 */
#define LOG_SETUP_IDLE   0u
#define LOG_SETUP_BUSY   1u
#define LOG_SETUP_READY  2u
#define LOG_SETUP_RETRY_AFTER 64u

static uint32_t log_setup_state;
static uint32_t log_setup_backoff;
static AstraLogProducer log_producer;
static uint32_t log_producer_endpoint; /* held only so death reaches the service */
static uint32_t log_port;

/* The doorbell. Never blocks: a full queue is WOULD_BLOCK, which means a
 * wake is already pending. Only PEER_DEAD reports the service gone. */
static uint32_t
log_ring_notify(void *context)
{
    static const uint32_t doorbell = ASTRA_LOG_DOORBELL_MAGIC;

    (void)context;
    return astra_port_send(log_port, &doorbell, sizeof(doorbell), NULL, 0u) ==
                   ASTRA_SYSCALL_PEER_DEAD ? 1u : 0u;
}

static void
log_ident(char out[ASTRA_LOG_IDENT_MAX])
{
    const char *path = astra_startup_argument(ASTRA_LOG_STARTUP_ADDRESS, 0u);
    const char *name = path;
    uint32_t length = 0u;

    if (path == NULL)
        path = name = "?";
    for (const char *at = path; *at != '\0'; ++at)
        if (*at == '/')
            name = at + 1;
    while (name[length] != '\0' && length + 1u < ASTRA_LOG_IDENT_MAX) {
        out[length] = name[length];
        ++length;
    }
    while (length < ASTRA_LOG_IDENT_MAX)
        out[length++] = '\0';
}

/* Returns 1 when the ring is ready after this call. */
static int
log_setup(void)
{
    const AstraStartupCapability *log;
    uint32_t area = 0u;
    uint32_t reader = 0u;
    uint32_t producer = 0u;
    uint32_t consumer = 0u;
    void *address = NULL;
    uint32_t size = 0u;
    AstraLogSetup setup;
    uint32_t handles[2];
    uint32_t expected = LOG_SETUP_IDLE;

    if (__atomic_load_n(&log_setup_state, __ATOMIC_ACQUIRE) == LOG_SETUP_READY)
        return 1;
    if (log_setup_backoff != 0u) {
        --log_setup_backoff; /* racy by design: a lost decrement retries later */
        return 0;
    }
    if (!__atomic_compare_exchange_n(&log_setup_state, &expected,
                                     LOG_SETUP_BUSY, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_RELAXED))
        return 0;
    log = astra_startup_capability(ASTRA_LOG_STARTUP_ADDRESS,
                                   ASTRA_CAPABILITY_LOG);
    if (log == NULL || log->handle == 0u)
        goto unavailable;
    if (astra_rt_area_create_flagged(
            ASTRA_LOG_RING_AREA_SIZE,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK)
        goto failed;
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &size) != ASTRA_SYSCALL_OK ||
        size < ASTRA_LOG_RING_AREA_SIZE)
        goto failed;
    if (astra_rt_ring_create(area, ASTRA_LOG_RING_OFFSET,
                             ASTRA_LOG_RING_SLOT_SIZE,
                             ASTRA_LOG_RING_CAPACITY, 0u, &producer,
                             &consumer) != ASTRA_SYSCALL_OK)
        goto failed;
    if (astra_rt_handle_duplicate(area, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                            ASTRA_RIGHT_MAP,
                                  &reader) != ASTRA_SYSCALL_OK)
        goto failed;
    setup.magic = ASTRA_LOG_SETUP_MAGIC;
    setup.version = ASTRA_LOG_SETUP_VERSION;
    setup.area_size = ASTRA_LOG_RING_AREA_SIZE;
    setup.ring_offset = ASTRA_LOG_RING_OFFSET;
    setup.slot_size = ASTRA_LOG_RING_SLOT_SIZE;
    setup.capacity = ASTRA_LOG_RING_CAPACITY;
    log_ident(setup.ident);
    handles[0] = consumer;
    handles[1] = reader;
    if (astra_port_send(log->handle, &setup, sizeof(setup), handles, 2u) !=
        ASTRA_SYSCALL_OK)
        goto failed;  /* never blocks: a full queue is WOULD_BLOCK */
    /* Both handles moved to the service on success. */
    log_producer_endpoint = producer;
    log_port = log->handle;
    astra_log_producer_init(&log_producer, address, log_ring_notify, NULL);
    (void)astra_close(area); /* the mapping keeps the area alive */
    __atomic_store_n(&log_setup_state, LOG_SETUP_READY, __ATOMIC_RELEASE);
    return 1;

failed:
    if (reader != 0u)
        (void)astra_close(reader);
    if (consumer != 0u)
        (void)astra_close(consumer);
    if (producer != 0u)
        (void)astra_close(producer);
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    if (area != 0u)
        (void)astra_close(area);
unavailable:
    log_setup_backoff = LOG_SETUP_RETRY_AFTER;
    __atomic_store_n(&log_setup_state, LOG_SETUP_IDLE, __ATOMIC_RELEASE);
    return 0;
}

uint32_t
astra_log_line(uint32_t level, uint32_t stream, const char *text,
               uint32_t length)
{
    AstraLogRecordFields fields;
    AstraTimeSample now;

    if (text == NULL || level > ASTRA_EVENT_LEVEL_ERROR ||
        stream > ASTRA_LOG_STREAM_MAX || stream == ASTRA_LOG_STREAM_KERNEL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    while (length != 0u && (text[length - 1u] == '\n' ||
                            text[length - 1u] == '\r'))
        --length;
    /* An empty line is recorded: a program's blank line is part of its
     * output. Only the kernel-ring fallback cannot carry one. */
    /* debug is live commentary, never stored (event_store.c drops it). */
    if (level == ASTRA_EVENT_LEVEL_DEBUG || !log_setup())
        return length == 0u ? ASTRA_SYSCALL_OK :
                              log_write_level(text, length, level);
    astra_time_now(&now);
    fields = (AstraLogRecordFields){0};
    fields.monotonic_high = (uint32_t)(now.monotonic_ns >> 32);
    fields.monotonic_low = (uint32_t)now.monotonic_ns;
    fields.wall_high = (uint32_t)(now.wall_ns >> 32);
    fields.wall_low = (uint32_t)now.wall_ns;
    fields.utc_offset = now.utc_offset;
    fields.zone = now.zone;
    fields.level = (uint8_t)level;
    fields.stream = (uint8_t)stream;
    fields.flags = now.wall_valid ? ASTRA_LOG_FLAG_WALL_VALID : 0u;
    switch (astra_log_producer_write(&log_producer, &fields, text, length)) {
    case ASTRA_LOG_WRITE_DEAD:
        /* The service went away: kernel ring until a later setup works. */
        __atomic_store_n(&log_setup_state, LOG_SETUP_IDLE, __ATOMIC_RELEASE);
        log_setup_backoff = LOG_SETUP_RETRY_AFTER;
        return length == 0u ? ASTRA_SYSCALL_OK :
                              log_write_level(text, length, level);
    default:
        return ASTRA_SYSCALL_OK; /* written, or dropped and counted */
    }
}
```

`log_write_level` is defined below this code today. Move its definition above
`astra_log_line`, or forward-declare it.

Check that each `astra_rt_*` name and signature used above matches
`ndk/include/astra/runtime.h`, including the area unmap function's exact name.
Adjust the calls, not the design.

Route the existing API:

- `astra_log_write(bytes, length)` →
  `astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG, bytes, length)`.
- `astra_log_debug` stays `log_write_level(..., DEBUG)`.
- `astra_log_failure` and `astra_log_assertion` build their text into a
  `char line[ASTRA_LOG_FIRST_TEXT]` with the existing `append`/`append_number`
  helpers, then call
  `astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG, line, n)`.
- Delete `log_parts` and `LogPart`, which are now unused.

Declare `astra_log_line` in `ndk/include/astra/runtime.h` beside `astra_log`,
with a doc comment stating the routing rule. Export it.

Run: `make -C sw/userspace/runtime build/host/test_runtime && sw/userspace/runtime/build/host/test_runtime`
Expected: PASS.

- [ ] **Step 4: Full runtime, NDK and m68k check**

On beast: `~/astra-mg/locked make -C sw/userspace -j8 && ~/astra-mg/locked make -C sw/userspace test`.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sw/userspace/runtime ndk/include/astra/runtime.h sw/include/astra
git commit -m "feat(runtime): astra_log_line writes the process log ring"
```

---

### Task 5: Line formatter and log file with rotation (events library)

**Files:**
- Create: `sw/userspace/events/include/astra/log_format.h`, `sw/userspace/events/src/log_format.c`
- Create: `sw/userspace/events/include/astra/log_file.h`, `sw/userspace/events/src/log_file.c`
- Create: `sw/userspace/events/tests/test_log_file.c`
- Modify: `sw/userspace/events/Makefile`

**Interfaces:**
- Consumes: `AstraLogRecordFields` (Task 3), `astra_civil_from_unix_ns_zone`,
  `astra_civil_zone_unpack` (`sw/common/civil.c`).
- Produces:

  ```c
  typedef struct AstraLogLine {
      uint64_t monotonic_ns; uint64_t wall_ns; int32_t utc_offset; uint32_t zone;
      uint8_t wall_valid; uint8_t level; uint8_t stream; uint8_t truncated;
      uint32_t pid; const char *ident; const char *text; uint32_t length;
  } AstraLogLine;
  uint32_t astra_log_format(char *out, uint32_t capacity, const AstraLogLine *line);
  /* local date as YYYYMMDD, for midnight rotation; 0 when wall time invalid */
  uint32_t astra_log_local_day(const AstraLogLine *line);

  typedef struct AstraLogFileOps {
      void *context;
      int (*size)(void *context, const char *name, uint64_t *bytes); /* 0 = absent */
      int (*append)(void *context, const char *name, const void *bytes, uint32_t length);
      int (*rename)(void *context, const char *from, const char *to);
      int (*remove)(void *context, const char *name);
  } AstraLogFileOps;
  typedef struct AstraLogFile {
      AstraLogFileOps ops; const char *base;   /* "syslog" */
      uint64_t max_bytes; uint32_t keep;        /* rotations kept */
      uint64_t bytes; uint32_t day; int ready;
  } AstraLogFile;
  int astra_log_file_open(AstraLogFile *file, const AstraLogFileOps *ops,
                          const char *base, uint64_t max_bytes, uint32_t keep);
  /* Appends one batch; rotates first if the batch would cross max_bytes
   * or `day` differs from the last write's day. */
  int astra_log_file_append(AstraLogFile *file, const char *bytes,
                            uint32_t length, uint32_t day);
  ```

- [ ] **Step 1: Write the failing test**

`sw/userspace/events/tests/test_log_file.c`:

```c
#include <astra/log_file.h>
#include <astra/log_format.h>
#include <astra/event.h>
#include <astra/log_ring.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* In-memory directory of up to 8 files. */
static struct { char name[32]; char data[4096]; uint32_t length; int used; }
    files[8];

static int find(const char *name)
{
    for (int i = 0; i < 8; ++i)
        if (files[i].used && strcmp(files[i].name, name) == 0) return i;
    return -1;
}
static int fs_size(void *c, const char *n, uint64_t *b)
{ (void)c; int i = find(n); *b = i < 0 ? 0u : files[i].length; return 1; }
static int fs_append(void *c, const char *n, const void *b, uint32_t l)
{
    (void)c; int i = find(n);
    if (i < 0) { for (i = 0; files[i].used; ++i) {} files[i].used = 1;
                 strcpy(files[i].name, n); files[i].length = 0; }
    assert(files[i].length + l <= sizeof(files[i].data));
    memcpy(files[i].data + files[i].length, b, l); files[i].length += l;
    return 1;
}
static int fs_rename(void *c, const char *f, const char *t)
{
    (void)c; int i = find(f), j = find(t);
    if (i < 0) return 0; if (j >= 0) files[j].used = 0;
    strcpy(files[i].name, t); return 1;
}
static int fs_remove(void *c, const char *n)
{ (void)c; int i = find(n); if (i >= 0) files[i].used = 0; return 1; }

static const AstraLogFileOps ops = { NULL, fs_size, fs_append, fs_rename,
                                     fs_remove };

static void
test_format(void)
{
    char out[512];
    AstraLogLine line = {
        .monotonic_ns = 12345000000ull,
        /* 2026-10-05T18:03:22.117Z */
        .wall_ns = 1791223402117000000ull,
        .utc_offset = -14400, .zone = 0x45445400u, .wall_valid = 1,
        .level = ASTRA_EVENT_LEVEL_NOTICE, .stream = ASTRA_LOG_STREAM_STDOUT,
        .pid = 0x10000011u, .ident = "chocolate-doom",
        .text = "V_Init: allocate screens.", .length = 25u };
    uint32_t n = astra_log_format(out, sizeof(out), &line);

    out[n] = '\0';
    assert(strcmp(out, "2026-10-05T14:03:22.117-04:00 notice "
                       "chocolate-doom[10000011] stdout: "
                       "V_Init: allocate screens.\n") == 0);
    assert(astra_log_local_day(&line) == 20261005u);

    line.text = "bell\a and\nnewline"; line.length = 17u;
    line.level = ASTRA_EVENT_LEVEL_ERROR; line.stream = ASTRA_LOG_STREAM_STDERR;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strstr(out, " error chocolate-doom[10000011] stderr: "
                       "bell\\x07 and\\x0anewline\n") != NULL);

    /* No wall clock: boot-relative time. */
    line.wall_valid = 0;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strncmp(out, "boot+12.345 ", 12) == 0);
    assert(astra_log_local_day(&line) == 0u);

    /* Truncation marker and a tiny buffer never overflow. */
    line.truncated = 1; line.text = "x"; line.length = 1u;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strstr(out, "stderr: x [truncated]\n") != NULL);
    assert(astra_log_format(out, 8u, &line) <= 8u);
}

static void
test_rotation(void)
{
    AstraLogFile file;
    char line[100];

    memset(files, 0, sizeof(files));
    memset(line, 'a', sizeof(line) - 1u); line[sizeof(line) - 1u] = '\n';
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(find("syslog.1") < 0);
    /* Third line would cross 250 bytes: rotate first. */
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(find("syslog.1") >= 0 && files[find("syslog.1")].length == 200u);
    assert(files[find("syslog")].length == 100u);
    /* New local day rotates even when small. */
    assert(astra_log_file_append(&file, line, 100u, 20261006u));
    assert(files[find("syslog.2")].length == 200u);
    assert(files[find("syslog.1")].length == 100u);
    /* Keep only 3: the oldest falls off. */
    assert(astra_log_file_append(&file, line, 100u, 20261007u));
    assert(astra_log_file_append(&file, line, 100u, 20261008u));
    assert(find("syslog.4") < 0 && find("syslog.3") >= 0);
    /* A batch larger than max_bytes is written whole into a fresh file. */
    {
        char big[300]; memset(big, 'b', sizeof(big));
        assert(astra_log_file_append(&file, big, 300u, 20261008u));
        assert(files[find("syslog")].length == 300u);
    }
    /* Reopen picks up the existing size. */
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(file.bytes == 300u);
}

int
main(void)
{
    test_format();
    test_rotation();
    puts("LOG FILE PASS");
    return 0;
}
```

Verify the epoch constant before relying on it. On the Mac,
`date -u -r 1791223402` must print `Mon Oct  5 18:03:22 UTC 2026`. If it does
not, compute the right value with
`python3 -c "import datetime;print(int(datetime.datetime(2026,10,5,18,3,22,tzinfo=datetime.timezone.utc).timestamp()))"`
and use that, followed by `117000000`.

Makefile (`sw/userspace/events/Makefile`):

- the variable:

  ```make
  LOG_FILE_TEST := build/host/test_log_file
  ```

- the rule:

  ```make
  $(LOG_FILE_TEST): src/log_format.c src/log_file.c tests/test_log_file.c \
  		../../common/civil.c ../../common/divide.c
  	@mkdir -p $(@D)
  	$(HOST_CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Werror -pedantic -O2 $^ -o $@
  ```

- `$(LOG_FILE_TEST)` and `./$(LOG_FILE_TEST)` added to `test:`, and the same
  sources added to `sanitize:`;
- `src/log_format.c src/log_file.c` added to the m68k library sources.

If the path differs, check how the runtime Makefile reaches `sw/common` (its
`COMMON_C_SOURCES`) and use the same relative path.

Run: `make -C sw/userspace/events build/host/test_log_file`. Expected: link
failure.

- [ ] **Step 2: Implement `log_format.c`**

```c
/*
 * One record, one line, always: control bytes are escaped so a program cannot
 * split its line or dress the next one up as someone else's.
 */

#include <astra/log_format.h>

#include <astra/civil.h>
#include <astra/event.h>
#include <astra/log_ring.h>

static const char *const level_names[] = {
    "debug", "info", "notice", "warning", "error" };
static const char *const stream_names[] = {
    "log", "stdout", "stderr", "kernel" };

typedef struct Out { char *at; uint32_t used; uint32_t capacity; } Out;

static void put(Out *o, char c)
{ if (o->used < o->capacity) o->at[o->used++] = c; }
static void puts_(Out *o, const char *s) { while (*s) put(o, *s++); }
static void digits(Out *o, uint32_t value, uint32_t width)
{
    char buffer[10]; uint32_t n = 0u;
    do { buffer[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value != 0u || n < width);
    while (n != 0u) put(o, buffer[--n]);
}
static void hex(Out *o, uint32_t value, uint32_t width)
{
    static const char x[] = "0123456789abcdef";
    for (uint32_t shift = width * 4u; shift != 0u; shift -= 4u)
        put(o, x[(value >> (shift - 4u)) & 0xfu]);
}

static int
civil(const AstraLogLine *line, AstraCivilTime *time)
{
    AstraTimeZone zone;

    if (!line->wall_valid)
        return 0;
    astra_civil_zone_unpack(line->utc_offset, line->zone, &zone);
    return astra_civil_from_unix_ns_zone(line->wall_ns, &zone, time);
}

uint32_t
astra_log_local_day(const AstraLogLine *line)
{
    AstraCivilTime time;

    if (!civil(line, &time) || time.year < 0)
        return 0u;
    return (uint32_t)time.year * 10000u + time.month * 100u + time.day;
}

uint32_t
astra_log_format(char *out, uint32_t capacity, const AstraLogLine *line)
{
    Out o = { out, 0u, capacity };
    AstraCivilTime time;

    if (civil(line, &time)) {
        uint32_t offset = (uint32_t)(time.utc_offset < 0 ?
                                     -time.utc_offset : time.utc_offset);

        digits(&o, (uint32_t)time.year, 4u); put(&o, '-');
        digits(&o, time.month, 2u); put(&o, '-');
        digits(&o, time.day, 2u); put(&o, 'T');
        digits(&o, time.hour, 2u); put(&o, ':');
        digits(&o, time.minute, 2u); put(&o, ':');
        digits(&o, time.second, 2u); put(&o, '.');
        digits(&o, time.nanosecond / 1000000u, 3u);
        put(&o, time.utc_offset < 0 ? '-' : '+');
        digits(&o, offset / 3600u, 2u); put(&o, ':');
        digits(&o, offset / 60u % 60u, 2u);
    } else {
        uint32_t rest = 0u;
        uint64_t seconds = astra_divide_u64(line->monotonic_ns, 1000000000u,
                                            &rest);

        puts_(&o, "boot+");
        digits(&o, (uint32_t)seconds, 1u); put(&o, '.');
        digits(&o, rest / 1000000u, 3u);
    }
    put(&o, ' ');
    puts_(&o, level_names[line->level <= 4u ? line->level : 4u]);
    put(&o, ' ');
    puts_(&o, line->ident != NULL && line->ident[0] != '\0' ?
              line->ident : "?");
    put(&o, '['); hex(&o, line->pid, 8u); puts_(&o, "] ");
    puts_(&o, stream_names[line->stream <= 3u ? line->stream : 0u]);
    puts_(&o, ": ");
    for (uint32_t index = 0u; index < line->length; ++index) {
        unsigned char c = (unsigned char)line->text[index];

        if (c < 0x20u || c == 0x7fu) {
            puts_(&o, "\\x"); hex(&o, c, 2u);
        } else if (c == '\\') {
            puts_(&o, "\\\\");
        } else {
            put(&o, (char)c);
        }
    }
    if (line->truncated)
        puts_(&o, " [truncated]");
    /* The newline always fits: back up over text if the buffer is full. */
    if (o.used == o.capacity && o.capacity != 0u)
        --o.used;
    put(&o, '\n');
    return o.used;
}
```

Check two prototypes before compiling. Read `astra_civil_zone_unpack` in
`sw/common/civil.c:92` for its exact signature; it may fill `AstraTimeZone` or
two out-parameters. Also confirm where `astra_divide_u64` is declared
(`grep -rn "astra_divide_u64" sw/include ndk/include`), and add that include.

- [ ] **Step 3: Implement `log_file.c`**

```c
/*
 * Append-only text with size-and-midnight rotation: Haiku's syslog_output
 * (src/servers/syslog_daemon/syslog_output.cpp:74-91), with a day boundary as
 * newsyslog and logrotate have. I/O is supplied so the host test and the
 * service run the same code.
 */

#include <astra/log_file.h>

static void
numbered(char *out, const char *base, uint32_t number)
{
    uint32_t length = 0u;
    char digits[10];
    uint32_t count = 0u;

    while (base[length] != '\0' && length < 20u) {
        out[length] = base[length];
        ++length;
    }
    out[length++] = '.';
    do { digits[count++] = (char)('0' + number % 10u); number /= 10u; }
    while (number != 0u);
    while (count != 0u)
        out[length++] = digits[--count];
    out[length] = '\0';
}

static int
rotate(AstraLogFile *file)
{
    char from[32];
    char to[32];

    numbered(to, file->base, file->keep);
    (void)file->ops.remove(file->ops.context, to);
    for (uint32_t number = file->keep; number > 1u; --number) {
        numbered(from, file->base, number - 1u);
        numbered(to, file->base, number);
        (void)file->ops.rename(file->ops.context, from, to);
    }
    numbered(to, file->base, 1u);
    if (!file->ops.rename(file->ops.context, file->base, to))
        return 0;
    file->bytes = 0u;
    return 1;
}

int
astra_log_file_open(AstraLogFile *file, const AstraLogFileOps *ops,
                    const char *base, uint64_t max_bytes, uint32_t keep)
{
    if (file == NULL || ops == NULL || base == NULL || keep == 0u ||
        max_bytes == 0u)
        return 0;
    file->ops = *ops;
    file->base = base;
    file->max_bytes = max_bytes;
    file->keep = keep;
    file->day = 0u;
    file->bytes = 0u;
    file->ready = ops->size(ops->context, base, &file->bytes);
    return file->ready;
}

int
astra_log_file_append(AstraLogFile *file, const char *bytes, uint32_t length,
                      uint32_t day)
{
    if (file == NULL || !file->ready || length == 0u)
        return file != NULL && file->ready;
    if (file->bytes != 0u &&
        (file->bytes + length > file->max_bytes ||
         (day != 0u && file->day != 0u && day != file->day)) &&
        !rotate(file))
        return 0;
    if (day != 0u)
        file->day = day;
    if (!file->ops.append(file->ops.context, file->base, bytes, length))
        return 0;
    file->bytes += length;
    return 1;
}
```

Run: `make -C sw/userspace/events build/host/test_log_file && sw/userspace/events/build/host/test_log_file`
Expected: `LOG FILE PASS`.

Note one behaviour: on reopen, `file->day` is 0, so the first write after a
reboot does not rotate even on a new day. The next day change does. That is
correct: `day` is the day of the last write this boot, and a reboot already
wrote a boot marker into the file.

- [ ] **Step 4: Commit**

On beast, run `~/astra-mg/locked make -C sw/userspace/events test`. Expected:
all PASS.

```bash
git add sw/userspace/events
git commit -m "feat(events): log line format and rotating log file"
```

---

### Task 6: Client table, drain and merge (events library)

**Files:**
- Create: `sw/userspace/events/include/astra/log_clients.h`, `sw/userspace/events/src/log_clients.c`
- Create: `sw/userspace/events/tests/test_log_clients.c`
- Modify: `sw/userspace/events/Makefile`

**Interfaces:**
- Consumes: `astra_log_consumer_next` (Task 3), `astra_log_format` and `AstraLogLine` (Task 5).
- Produces:

  ```c
  #define ASTRA_LOG_CLIENTS_MAX 96u
  typedef struct AstraLogClient {
      uint32_t used, pid, consumer_handle, area_handle, position, last_dropped, dead;
      volatile const void *ring;
      char ident[ASTRA_LOG_IDENT_MAX];
  } AstraLogClient;
  typedef struct AstraLogClients { AstraLogClient client[ASTRA_LOG_CLIENTS_MAX]; } AstraLogClients;

  /* Validates a setup message and ring header; returns slot or NULL. */
  AstraLogClient *astra_log_clients_add(AstraLogClients *clients, uint32_t pid,
      const AstraLogSetup *setup, uint32_t setup_size,
      volatile const void *ring, uint32_t mapped_size,
      uint32_t consumer_handle, uint32_t area_handle);

  /* Drains every client, merges by monotonic time, and formats into `out`.
   * Calls `consumed(context, client)` once per client whose position moved,
   * so the service notifies the kernel *before* publishing the position.
   * Returns bytes written into out. */
  typedef void (*AstraLogConsumed)(void *context, AstraLogClient *client,
                                   uint32_t position);
  uint32_t astra_log_clients_drain(AstraLogClients *clients, char *out,
      uint32_t capacity, AstraLogConsumed consumed, void *context,
      uint32_t *day);
  ```

- [ ] **Step 1: Write the failing test**

`sw/userspace/events/tests/test_log_clients.c`:

```c
#include <astra/log_clients.h>
#include <astra/log_ring.h>
#include <astra/event.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t ring_a[ASTRA_LOG_RING_AREA_SIZE];
static uint8_t ring_b[ASTRA_LOG_RING_AREA_SIZE];
static AstraLogClients clients;
static uint32_t consumed_calls;

static void
format(uint8_t *area)
{
    AstraBulkRingHeader *header = (void *)area;

    memset(area, 0, ASTRA_LOG_RING_AREA_SIZE);
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->element_size = ASTRA_LOG_RING_SLOT_SIZE;
    header->capacity = ASTRA_LOG_RING_CAPACITY;
    header->data_offset = ASTRA_LOG_RING_HEADER_SIZE;
}

static void
emit(uint8_t *area, AstraLogProducer *p, uint64_t monotonic, const char *text)
{
    AstraLogRecordFields f;

    memset(&f, 0, sizeof(f));
    f.monotonic_high = (uint32_t)(monotonic >> 32);
    f.monotonic_low = (uint32_t)monotonic;
    f.level = ASTRA_EVENT_LEVEL_NOTICE;
    f.stream = ASTRA_LOG_STREAM_STDOUT;
    (void)area;
    assert(astra_log_producer_write(p, &f, text, (uint32_t)strlen(text)) ==
           ASTRA_LOG_WRITE_OK);
}

static void
on_consumed(void *context, AstraLogClient *client, uint32_t position)
{
    (void)context;
    ++consumed_calls;
    ((AstraBulkRingHeader *)(uintptr_t)client->ring)->consumer_position =
        position;
}

static AstraLogSetup
setup(const char *ident)
{
    AstraLogSetup s;

    memset(&s, 0, sizeof(s));
    s.magic = ASTRA_LOG_SETUP_MAGIC;
    s.version = ASTRA_LOG_SETUP_VERSION;
    s.area_size = ASTRA_LOG_RING_AREA_SIZE;
    s.ring_offset = ASTRA_LOG_RING_OFFSET;
    s.slot_size = ASTRA_LOG_RING_SLOT_SIZE;
    s.capacity = ASTRA_LOG_RING_CAPACITY;
    strcpy(s.ident, ident);
    return s;
}

int
main(void)
{
    AstraLogProducer a, b;
    AstraLogSetup sa = setup("doom"), sb = setup("display");
    char out[4096];
    uint32_t day = 0u;
    uint32_t n;

    format(ring_a);
    format(ring_b);
    astra_log_producer_init(&a, ring_a, NULL, NULL);
    astra_log_producer_init(&b, ring_b, NULL, NULL);
    assert(astra_log_clients_add(&clients, 0x10000011u, &sa, sizeof(sa),
                                 ring_a, sizeof(ring_a), 5u, 6u) != NULL);
    assert(astra_log_clients_add(&clients, 0x10000012u, &sb, sizeof(sb),
                                 ring_b, sizeof(ring_b), 7u, 8u) != NULL);

    /* Bad setups are refused. */
    {
        AstraLogSetup bad = sa;
        bad.slot_size = 256u;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad) - 1u,
                                     ring_a, sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        bad.ident[ASTRA_LOG_IDENT_MAX - 1u] = 'x'; /* not terminated */
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        assert(astra_log_clients_add(&clients, 1u, &sa, sizeof(sa), ring_a,
                                     4096u, 9u, 10u) == NULL); /* too small */
    }

    emit(ring_a, &a, 300u, "a-late");
    emit(ring_b, &b, 100u, "b-early");
    emit(ring_a, &a, 200u, "a-middle");
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day);
    out[n] = '\0';
    /* Merged by monotonic time across clients, each line once. */
    {
        const char *early = strstr(out, "display[10000012] stdout: b-early");
        const char *middle = strstr(out, "doom[10000011] stdout: a-middle");
        const char *late = strstr(out, "doom[10000011] stdout: a-late");

        assert(early && middle && late && early < middle && middle < late);
    }
    assert(consumed_calls == 2u);

    /* Nothing new: no output, no consumed calls. */
    assert(astra_log_clients_drain(&clients, out, sizeof(out), on_consumed,
                                   NULL, &day) == 0u);
    assert(consumed_calls == 2u);

    /* Drops are announced once, before the next line. */
    a.dropped = 3u;
    emit(ring_a, &a, 400u, "after-drop");
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day);
    out[n] = '\0';
    assert(strstr(out, "warning doom[10000011] log: [3 lines dropped]\n"));
    assert(strstr(out, "[3 lines dropped]") < strstr(out, "after-drop"));

    /* A corrupt ring marks the client dead and stops reading it. */
    ((AstraBulkRingHeader *)ring_b)->producer_position = 100000u;
    (void)astra_log_clients_drain(&clients, out, sizeof(out), on_consumed,
                                  NULL, &day);
    assert(clients.client[1].dead);

    puts("LOG CLIENTS PASS");
    return 0;
}
```

Add a Makefile rule for `$(LOG_CLIENTS_TEST)` linking `src/log_clients.c`,
`src/log_format.c`, `$(RUNTIME)/src/log_ring.c`, the two `sw/common` sources and
the test, in the same style as Task 5. Register it in `test:` and `sanitize:`.

Expected: link failure.

- [ ] **Step 2: Implement `log_clients.c`**

```c
/*
 * Every process's ring, drained into one time-ordered batch. The merge is per
 * drain pass: records within a pass are ordered by monotonic time across
 * processes, and passes follow one another, so the file is in order to within
 * one pass (docs/SYSTEM_LOG.md section 5).
 */

#include <astra/log_clients.h>

#include <astra/event.h>
#include <astra/log_format.h>
#include <astra/log_ring.h>

#define DRAIN_BATCH 128u

typedef struct Pending {
    AstraLogRecordFields fields;
    AstraLogClient *client;
    uint32_t length;
    char text[ASTRA_LOG_TEXT_MAX];
} Pending;

static Pending pending[DRAIN_BATCH];
static Pending *order[DRAIN_BATCH];

static uint64_t
monotonic(const AstraLogRecordFields *f)
{
    return ((uint64_t)f->monotonic_high << 32) | f->monotonic_low;
}

AstraLogClient *
astra_log_clients_add(AstraLogClients *clients, uint32_t pid,
                      const AstraLogSetup *setup, uint32_t setup_size,
                      volatile const void *ring, uint32_t mapped_size,
                      uint32_t consumer_handle, uint32_t area_handle)
{
    const volatile AstraBulkRingHeader *header = ring;
    uint32_t terminated = 0u;

    if (clients == NULL || setup == NULL || ring == NULL ||
        setup_size != sizeof(*setup) ||
        setup->magic != ASTRA_LOG_SETUP_MAGIC ||
        setup->version != ASTRA_LOG_SETUP_VERSION ||
        setup->area_size != ASTRA_LOG_RING_AREA_SIZE ||
        setup->ring_offset != ASTRA_LOG_RING_OFFSET ||
        setup->slot_size != ASTRA_LOG_RING_SLOT_SIZE ||
        setup->capacity != ASTRA_LOG_RING_CAPACITY ||
        mapped_size < ASTRA_LOG_RING_AREA_SIZE ||
        header->magic != ASTRA_BULK_RING_MAGIC ||
        header->element_size != ASTRA_LOG_RING_SLOT_SIZE ||
        header->capacity != ASTRA_LOG_RING_CAPACITY ||
        header->data_offset != ASTRA_LOG_RING_HEADER_SIZE)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LOG_IDENT_MAX; ++index)
        if (setup->ident[index] == '\0')
            terminated = 1u;
    if (!terminated)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        AstraLogClient *client = &clients->client[index];

        if (client->used)
            continue;
        client->used = 1u;
        client->pid = pid;
        client->consumer_handle = consumer_handle;
        client->area_handle = area_handle;
        client->ring = ring;
        client->position = header->consumer_position;
        client->last_dropped = 0u;
        client->dead = 0u;
        for (uint32_t at = 0u; at < ASTRA_LOG_IDENT_MAX; ++at)
            client->ident[at] = setup->ident[at];
        return client;
    }
    return NULL;
}

static uint32_t
append_line(char *out, uint32_t capacity, uint32_t used,
            const AstraLogRecordFields *f, const AstraLogClient *client,
            const char *text, uint32_t length, uint32_t *day)
{
    AstraLogLine line;

    line.monotonic_ns = monotonic(f);
    line.wall_ns = ((uint64_t)f->wall_high << 32) | f->wall_low;
    line.utc_offset = f->utc_offset;
    line.zone = f->zone;
    line.wall_valid = (f->flags & ASTRA_LOG_FLAG_WALL_VALID) != 0u;
    line.level = f->level;
    line.stream = f->stream;
    line.truncated = (f->flags & ASTRA_LOG_FLAG_TRUNCATED) != 0u;
    line.pid = client->pid;
    line.ident = client->ident;
    line.text = text;
    line.length = length;
    if (astra_log_local_day(&line) != 0u)
        *day = astra_log_local_day(&line);
    if (used >= capacity)
        return used;
    return used + astra_log_format(out + used, capacity - used, &line);
}

static uint32_t
dropped_text(char *out, uint32_t count)
{
    char digits[10];
    uint32_t n = 0u;
    uint32_t used = 0u;
    static const char tail[] = " lines dropped]";

    out[used++] = '[';
    do { digits[n++] = (char)('0' + count % 10u); count /= 10u; }
    while (count != 0u);
    while (n != 0u)
        out[used++] = digits[--n];
    for (uint32_t index = 0u; tail[index] != '\0'; ++index)
        out[used++] = tail[index];
    return used;
}

uint32_t
astra_log_clients_drain(AstraLogClients *clients, char *out,
                        uint32_t capacity, AstraLogConsumed consumed,
                        void *context, uint32_t *day)
{
    uint32_t count = 0u;
    uint32_t used = 0u;

    for (uint32_t index = 0u;
         index < ASTRA_LOG_CLIENTS_MAX && count < DRAIN_BATCH; ++index) {
        AstraLogClient *client = &clients->client[index];
        uint32_t start;

        if (!client->used || client->dead)
            continue;
        start = client->position;
        while (count < DRAIN_BATCH) {
            Pending *p = &pending[count];
            uint32_t status = astra_log_consumer_next(
                client->ring, &client->position, &p->fields, p->text,
                sizeof(p->text), &p->length);

            if (status == ASTRA_LOG_READ_EMPTY)
                break;
            if (status == ASTRA_LOG_READ_CORRUPT) {
                client->dead = 1u;
                break;
            }
            p->client = client;
            order[count] = p;
            ++count;
        }
        if (client->position != start && consumed != NULL)
            consumed(context, client, client->position);
    }
    /* Insertion sort: a batch is at most 128 and nearly sorted. Stable, so
     * one process's lines keep their order on equal timestamps. */
    for (uint32_t i = 1u; i < count; ++i) {
        Pending *item = order[i];
        uint32_t j = i;

        while (j != 0u &&
               monotonic(&order[j - 1u]->fields) > monotonic(&item->fields)) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = item;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        Pending *p = order[i];
        AstraLogClient *client = p->client;

        if (p->fields.dropped != client->last_dropped) {
            AstraLogRecordFields note = p->fields;
            char text[32];
            uint32_t length = dropped_text(
                text, p->fields.dropped - client->last_dropped);

            note.level = ASTRA_EVENT_LEVEL_WARNING;
            note.stream = ASTRA_LOG_STREAM_LOG;
            note.flags &= (uint8_t)~ASTRA_LOG_FLAG_TRUNCATED;
            used = append_line(out, capacity, used, &note, client, text,
                               length, day);
            client->last_dropped = p->fields.dropped;
        }
        used = append_line(out, capacity, used, &p->fields, client, p->text,
                           p->length, day);
    }
    return used;
}
```

`Pending` holds 1,944 bytes of text per entry. 128 entries is about 250 KiB of
BSS in the events service, which is acceptable for a service on a 512 MiB
machine but worth checking. Run `m68k-astra-size` on `events.elf` in Task 7 and
record the number in the commit message. If it measures worse than expected,
make `DRAIN_BATCH` 32.

Run: `make -C sw/userspace/events build/host/test_log_clients && sw/userspace/events/build/host/test_log_clients`
Expected: `LOG CLIENTS PASS`. Then run `make -C sw/userspace/events sanitize` on
beast. Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add sw/userspace/events
git commit -m "feat(events): drain and merge every process's log ring"
```

---

### Task 7: The events service becomes the log service

**Files:**
- Modify: `sw/userspace/services/events/main.c`
- Modify: `sw/userspace/events/src/event_backend.c`, `sw/userspace/events/include/astra/event_backend.h`
- Modify: `sw/userspace/events/tests/test_event_backend.c`
- Modify: `emu/qemu/astra_image.py` (manifest line)

**Interfaces:**
- Consumes: Tasks 3, 5 and 6.
- Produces:
  - the `LOG` endpoint, published third in `astra_service_ready`;
  - `/events/syslog` and `/events/syslog.N` on the boot volume;
  - `WORK:.astra/syslog` on the host;
  - `EVENTS:log/syslog`.

- [ ] **Step 1: Backend — failing test for `log/syslog`**

In `event_backend.h`, add to `AstraEventsBackend`:

```c
    /* Bytes of the current syslog, or NULL when the service has none. */
    int (*log_read)(void *context, uint64_t offset, void *buffer,
                    uint32_t length, uint32_t *moved);
    void *log_context;
```

In `tests/test_event_backend.c`, add a test that does the following:

- set `log_read` to a fake over a static string;
- `readdir("/")` lists `"log"` at cookie 3;
- `readdir("/log")` lists `"syslog"` as a FILE;
- `open("/log/syslog")` then `read` at offsets 0 and 5 returns the string's
  bytes from those offsets;
- `open("/log/syslog", WRITE)` is refused;
- with `log_read == NULL`, `/log` is NOT_FOUND.

Run: `make -C sw/userspace/events build/host/test_event_backend && sw/userspace/events/build/host/test_event_backend`
Expected: FAIL.

- [ ] **Step 2: Backend — implement**

In `event_backend.c`:

- Add `KIND_LOG_DIR 9` and `KIND_LOG_FILE 10`.
- In `describe`: `/log` → `KIND_LOG_DIR` and `/log/syslog` → `KIND_LOG_FILE`,
  both only when `backend->log_read != NULL`.
- Add `KIND_LOG_DIR` to `is_directory`.
- In `readdir`: ROOT cookie 3 → `"log"` (only when `log_read` is set); for
  `KIND_LOG_DIR`, cookie 0 → `"syslog"` with `info->kind = FILE`.
- At the top of `events_read`, when the node kind is `KIND_LOG_FILE`, return
  `backend->log_read(backend->log_context, offset, buffer, length, moved)`
  mapped to `ASTRA_VFS_OK`/`ASTRA_VFS_ERR_IO`.

Rerun. Expected: PASS.

- [ ] **Step 3: Service — LOG port, clients and sinks**

In `sw/userspace/services/events/main.c` (no `event_emit.h`, no `astra_log*`):

1. Includes: `<astra/log_clients.h>`, `<astra/log_file.h>`,
   `<astra/log_ring.h>`, `<astra/time_page.h>`.

2. State:

   ```c
   #define LOG_PORT_MESSAGES 16u
   #define LOG_MAX_BYTES (1024u * 1024u)
   #define LOG_KEEP 3u
   static uint32_t log_handle;        /* published as LOG */
   static uint32_t log_receive;
   static AstraLogClients log_clients;
   static AstraLogFile log_store;      /* /store/syslog */
   static AstraLogFile log_host;       /* /work/.astra/syslog */
   static int log_host_ready;
   static char log_batch[64u * 1024u];
   ```

3. `AstraLogFileOps` over `astra_filesystem_*` with
   `&process_filesystem.filesystem`:
   - `size`: `astra_filesystem_stat`. Return 1 with size 0 when the file is
     `ASTRA_VFS_ERR_NOT_FOUND`, 1 with the size on OK, 0 otherwise.
   - `append`: `astra_filesystem_open(..., ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_APPEND, &f)`,
     then `astra_filesystem_write` loop until done (`moved == 0` is failure),
     then `close`.
   - `rename` and `remove`: `astra_filesystem_rename` and
     `astra_filesystem_unlink`. NOT_FOUND counts as success for `remove`.

   The context is the directory prefix (`"/store/"` or `"/work/.astra/"`), and
   each op joins prefix + name into a 64-byte buffer with
   `astra_string_concat`.

4. In `events_start`, after the control port:

   ```c
   if (astra_rt_port_create(LOG_PORT_MESSAGES,
                            LOG_PORT_MESSAGES * (uint32_t)sizeof(AstraLogSetup),
                            &log_receive, &log_handle) != ASTRA_SYSCALL_OK)
       return EVENTS_FAIL_LOG_PORT;
   (void)astra_log_file_open(&log_store, &store_ops, "syslog",
                             LOG_MAX_BYTES, LOG_KEEP);
   (void)astra_filesystem_mkdir(&process_filesystem.filesystem,
                                "/work/.astra");
   log_host_ready = astra_log_file_open(&log_host, &host_ops, "syslog",
                                        LOG_MAX_BYTES, LOG_KEEP);
   log_boot_marker();
   backend.log_read = syslog_read;   /* astra_filesystem_read_at on /store/syslog */
   backend.log_context = NULL;
   ```

   Add `EVENTS_FAIL_LOG_PORT` to the fail enum. A failed log file open is not a
   service failure: lines are then drained and discarded, and the service still
   serves `EVENTS:`.

   `log_boot_marker()` formats
   `--- boot <ISO wall time> build <ASTRA_BUILD_ID as 8 hex> ---\n` from
   `astra_time_now`, using the same civil helpers as `log_format.c` (expose a
   small `astra_log_format_time(char *out, uint32_t cap, const AstraTimeSample *)`
   from `log_format.c` rather than duplicating it). It appends the marker to
   both files.

5. `astra_main`: publish three handles `{events_handle, control_handle, log_handle}`.

6. Accept setups in `events_pump`, before the trace drain:

   ```c
   for (;;) {
       AstraLogSetup setup;
       uint32_t handles[2] = {0u, 0u};
       uint32_t size = 0u, count = 0u, sender = 0u;
       void *address = NULL;
       uint32_t mapped = 0u;

       if (astra_port_receive_from(log_receive, &setup, sizeof(setup),
                                   handles, 2u, &size, &count, &sender) !=
           ASTRA_SYSCALL_OK)
           break;
       if (size == sizeof(uint32_t) && count == 0u)
           continue; /* a doorbell: the drain below reads every ring */
       if (count != 2u ||
           astra_rt_area_map(handles[1],
                             ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                             &address, &mapped) != ASTRA_SYSCALL_OK ||
           astra_log_clients_add(&log_clients, sender, &setup, size, address,
                                 mapped, handles[0], handles[1]) == NULL) {
           if (address != NULL)
               (void)astra_rt_area_unmap(address);
           for (uint32_t i = 0u; i < count; ++i)
               (void)astra_close(handles[i]);
       }
   }
   ```

   Check `astra_port_receive_from`'s exact semantics on an empty port. It
   should return `WOULD_BLOCK` without blocking; if it blocks, this loop
   instead waits for readiness first. Confirm with
   `grep -n "receive_from\|PORT_RECEIVE" sw/userspace/runtime/src/port.c sw/include/astra/syscall.h`.

7. Drain the rings in `events_pump`, after the trace drain. Write the kernel
   ring's text records into the same batch first. Render each joined
   `AstraEventDrained` text with ident `kernel` when the pid is 0, otherwise
   the ident of the client with that pid, otherwise `?`. Its stream is
   `ASTRA_LOG_STREAM_KERNEL` for pid 0, otherwise `ASTRA_LOG_STREAM_LOG`.

   The kernel ring's timestamps are monotonic. Convert them to wall time with
   one `astra_time_now` sample per pass:
   `wall = sample.wall_ns - sample.monotonic_ns + record_monotonic`.
   Find the drained record's timestamp field name in `sw/include/astra/event.h`
   (`AstraEventDrained`).

   Then:

   ```c
   uint32_t day = 0u;
   uint32_t used = kernel_text_into(log_batch, sizeof(log_batch), &day);

   used += astra_log_clients_drain(&log_clients, log_batch + used,
                                   sizeof(log_batch) - used, log_consumed,
                                   NULL, &day);
   if (used != 0u) {
       (void)astra_log_file_append(&log_store, log_batch, used, day);
       if (log_host_ready)
           (void)astra_log_file_append(&log_host, log_batch, used, day);
   }
   reap_dead_clients();
   ```

   `log_consumed` only publishes the consumer position in the shared header.
   It never notifies the kernel ring object (`docs/SYSTEM_LOG.md` §3, Doorbell):

   ```c
   static void
   log_consumed(void *context, AstraLogClient *client, uint32_t position)
   {
       (void)context;
       __atomic_store_n(&((volatile AstraBulkRingHeader *)(uintptr_t)
                          client->ring)->consumer_position,
                        position, __ATOMIC_RELEASE);
   }
   ```

   The area is mapped read-only in the service (step 6) but the consumer
   position must be written. Map it `ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE`
   and send a `READ|WRITE|MAP` duplicate from the client (Task 4: change the
   duplicate's rights to `ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP`).
   The service still trusts nothing it reads from the area: every record is
   validated by `astra_log_consumer_next`.

   `reap_dead_clients` does the following for each client:
   - **Dead (corrupt):** unmap the ring, close both handles, and clear the slot.
   - **Producer gone:** check with
     `astra_wait_one(client->consumer_handle, ASTRA_DEADLINE_POLL, ...)`. The
     kernel's shadow positions stay 0 because nobody notifies, so the endpoint
     is never "ready". It reports `PEER_DEAD` once the producer process has
     died. Then drain the ring one last time and reap the client. Copy how
     `vfs_port_transport.c:2075-2122` polls its sessions.

   Run the setup-accept and drain loops until a pass moves nothing, so a burst
   larger than one batch drains within one wake.

8. Wait loop in `astra_main`: `waits[]` stays the receive ports:
   `control_receive`, `events_receive` and `log_receive`. Doorbells arrive on
   `log_receive`. Keep the one-second deadline: it is the backstop sweep, and
   the moment dead producers are reaped.

- [ ] **Step 4: Manifest**

In `emu/qemu/astra_image.py:175-176`, replace the events line with:

```python
    "service /services/events grants SYSTEM:r STORE:rw WORK:rw LIBS:r "
    "serves EVENTS:r EVENT_CONTROL LOG required priority=12\n"
```

Add `LOG` to `TERMINAL_CEILING` (`astra_image.py:128-132`).

`events` is listed after `hostfs` in the manifest, which serves `WORK`. Check
that line order: `WORK:rw` is granted by name only if hostfs has already
started (`loader.c:996-1015`).

- [ ] **Step 5: Build, size, unit tests**

On beast:
`~/astra-mg/locked make -C sw/userspace -j8 && ~/astra-mg/locked make -C sw/userspace test && m68k-astra-size sw/userspace/services/events/build/m68k/events.elf`

Expected: PASS. Record the BSS size.

- [ ] **Step 6: Commit**

```bash
git add sw/userspace/services/events sw/userspace/events emu/qemu/astra_image.py
git commit -m "feat(events): drain log rings into /events/syslog and the host"
```

---

### Task 8: Every process gets LOG

**Files:**
- Modify: `sw/userspace/supervisor/src/loader.c` (`build_grants`)
- Modify: `sw/userspace/services/terminal/console_session.c` (`launch_grants`)
- Test: the supervisor's host tests (`grep -ln build_grants sw/userspace/supervisor/tests/*.c`)

**Interfaces:**
- Consumes: the `LOG` service handle published in Task 7.
- Produces: a `LOG` startup capability in every launched entry except the one
  that serves it.

- [ ] **Step 1: Failing supervisor test**

In the supervisor test file that covers `build_grants`, which is the one
asserting the `LIBS` fallback (find it with `grep -ln '"LIBS"' sw/userspace/supervisor/tests`),
add the following cases:

- an entry with no `LOG` grant receives `LOG` with `ASTRA_RIGHT_SIGNAL` once a
  service named `LOG` is published;
- the entry that serves `LOG` does not receive it;
- with no `LOG` published, the entry launches without it, not with an error.

Expected: FAIL.

- [ ] **Step 2: Implement in `build_grants`**

Add after the LIBS fallback (`loader.c:1017-1034`):

```c
    /* Every program may log (docs/SYSTEM_LOG.md section 7). The service that
     * drains logs is excluded: it must never log about logging. Before it has
     * started there is nothing to grant, and those programs use LOG_WRITE. */
    if (!entry_grants(entry, ASTRA_CAPABILITY_LOG) &&
        !entry_serves(entry, ASTRA_CAPABILITY_LOG)) {
        uint32_t published = named_service(ASTRA_CAPABILITY_LOG);

        if (published != 0u) {
            status = add_grant(out, count, ASTRA_CAPABILITY_LOG, published,
                               ASTRA_RIGHT_SIGNAL, 0u, NULL);
            if (status != ASTRA_STATUS_OK)
                return status;
        }
    }
```

Include `<astra/log_ring.h>` for the name.

`add_grant` must not fail when an entry has already used every grant slot.
Check its capacity constant against the largest manifest entry (Terminal) plus
one.

Run the supervisor host tests. Expected: PASS.

- [ ] **Step 3: Terminal forwards LOG**

In `console_session.c:106-117`, add `ASTRA_CAPABILITY_LOG` to
`authority_names`. That grants it to every command Terminal launches. zsh
children inherit it through exec (`process.c:4685-4700`).

- [ ] **Step 4: Build and commit**

On beast: `~/astra-mg/locked make -C sw/userspace -j8 && ~/astra-mg/locked make -C sw/userspace test`.
Expected: PASS.

```bash
git add sw/userspace/supervisor sw/userspace/services/terminal
git commit -m "feat(supervisor): grant LOG to every program"
```

---

### Task 9: POSIX stdout/stderr without a terminal

**Files:**
- Modify: `sw/userspace/posix/src/console.c`
- Test: `sw/userspace/posix/tests/test_descriptor.c`

**Interfaces:**
- Consumes: `astra_log_line` (Task 4).
- Produces: fd 1 → level notice, stream stdout; fd 2 → level warning, stream
  stderr; one line per newline per thread; partial lines flushed at `_exit`.

- [ ] **Step 1: Failing tests**

In `test_descriptor.c`, replace the discarding `astra_log` stub with a
recording `astra_log_line` stub:

```c
static struct { uint32_t level, stream; char text[512]; } logged[32];
static uint32_t logged_count;

uint32_t
astra_log_line(uint32_t level, uint32_t stream, const char *text,
               uint32_t length)
{
    assert(logged_count < 32u && length < 512u);
    logged[logged_count].level = level;
    logged[logged_count].stream = stream;
    memcpy(logged[logged_count].text, text, length);
    logged[logged_count].text[length] = '\0';
    ++logged_count;
    return ASTRA_SYSCALL_OK;
}
```

Keep the `astra_log` stub too if `console.c` still calls it elsewhere. Then add:

```c
static void
test_log_descriptors_without_streams(void)
{
    AstraStartupInfo startup = {
        .magic = ASTRA_STARTUP_MAGIC,
        .abi_version = ASTRA_STARTUP_ABI_VERSION,
        .program_entry = 0x00100000u,
        .header_size = ASTRA_STARTUP_INFO_SIZE,
        .total_size = ASTRA_STARTUP_INFO_SIZE,
        .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
        .capability_count = 0u,
    };

    logged_count = 0u;
    astra_posix_start(&startup);
    assert(!isatty(1) && !isatty(2));
    /* stderr arrives a byte at a time from picolibc; one record per line. */
    assert(write(2, "e", 1) == 1 && write(2, "rr\n", 3) == 3);
    assert(write(1, "out one\nout two\n", 16) == 16);
    assert(logged_count == 3u);
    assert(strcmp(logged[0].text, "err") == 0 &&
           logged[0].stream == ASTRA_LOG_STREAM_STDERR &&
           logged[0].level == ASTRA_EVENT_LEVEL_WARNING);
    assert(strcmp(logged[1].text, "out one") == 0 &&
           logged[1].stream == ASTRA_LOG_STREAM_STDOUT &&
           logged[1].level == ASTRA_EVENT_LEVEL_NOTICE);
    assert(strcmp(logged[2].text, "out two") == 0);
    /* Interleaved partial lines on the two streams stay separate. */
    assert(write(1, "par", 3) == 3 && write(2, "tial", 4) == 4);
    assert(logged_count == 3u);
    assert(write(1, "t\n", 2) == 2);
    assert(strcmp(logged[3].text, "part") == 0);
    /* Blank lines are kept: a program's blank line is part of its output. */
    assert(write(1, "\n", 1) == 1);
    assert(logged_count == 5u && logged[4].text[0] == '\0');
    /* A 300-byte line is emitted in 240-byte pieces. */
    {
        char line[300];
        memset(line, 'z', sizeof(line));
        assert(write(1, line, sizeof(line)) == 300);
        assert(logged_count == 6u && strlen(logged[5].text) == 240u);
    }
    /* _exit's flush emits what is pending on both streams. */
    astra_posix_log_flush();
    assert(logged_count == 8u);
    assert(strcmp(logged[6].text, "tial") == 0 ||
           strcmp(logged[7].text, "tial") == 0);
}

static void
test_log_descriptors_survive_exec(void)
{
    /* astra_posix_start without streams, export, restart from handoff:
     * fd 1 still logs as stdout and fd 2 as stderr. Follow the exec
     * round-trip pattern at test_descriptor.c:451-477. */
}
```

Fill in `test_log_descriptors_survive_exec` with the existing round-trip
pattern, then assert `logged[...].stream` after one write to each fd.

The blank-line assertion is a change from today: `log_write` drops blank lines
(`used != 0u`). `astra_log_line` records an empty line (Task 4,
`test_log_line_records_empty_lines`).

Run: `make -C sw/userspace/posix build/host/test_descriptor && sw/userspace/posix/build/host/test_descriptor`
on beast (the rule uses GNU ld flags). Expected: FAIL.

- [ ] **Step 2: Implement**

In `console.c`:

1. **Description fields.** The LOG description's `value` becomes the stream
   (`ASTRA_LOG_STREAM_STDOUT` or `ASTRA_LOG_STREAM_STDERR`). In
   `release_description` (`:267-298`), skip `astra_close(value)` when
   `kind == POSIX_DESCRIPTOR_LOG`; today it is skipped only because `value` is
   0.

2. **Separate descriptions.** In `astra_posix_start`, replace the
   `:1028-1039` block:

   ```c
   if (descriptors[POSIX_STDOUT].description == NULL &&
       descriptors[POSIX_STDERR].description == NULL) {
       for (uint32_t fd = POSIX_STDOUT; fd <= POSIX_STDERR; ++fd) {
           initial_descriptions[fd].kind = POSIX_DESCRIPTOR_LOG;
           initial_descriptions[fd].status_flags = O_WRONLY;
           initial_descriptions[fd].value = fd == POSIX_STDOUT ?
               ASTRA_LOG_STREAM_STDOUT : ASTRA_LOG_STREAM_STDERR;
           initial_descriptions[fd].references = 1u;
           descriptors[fd].description = &initial_descriptions[fd];
       }
   }
   ```

   The exec export and restore already copy `value` (`:679-683`, `:847-867`).
   Make sure restore does not reject a nonzero `value` for LOG.

3. **Per-thread line assembly.** Replace `log_write`:

   ```c
   #define POSIX_LOG_LINE_MAX 240u

   typedef struct PosixLogLine {
       char text[POSIX_LOG_LINE_MAX];
       uint8_t used;
       uint8_t pending; /* bytes waiting, even zero of them after a newline */
   } PosixLogLine;

   /* One per thread per stream: picolibc's stderr writes a byte at a time,
    * and threads must not interleave inside one line. No lock. */
   static _Thread_local PosixLogLine log_lines[2];

   static uint32_t
   log_level(uint32_t stream)
   {
       return stream == ASTRA_LOG_STREAM_STDERR ? ASTRA_EVENT_LEVEL_WARNING :
                                                  ASTRA_EVENT_LEVEL_NOTICE;
   }

   static void
   log_emit(uint32_t stream, PosixLogLine *line)
   {
       (void)astra_log_line(log_level(stream), stream, line->text, line->used);
       line->used = 0u;
       line->pending = 0u;
   }

   static ssize_t
   log_write(uint32_t stream, const void *bytes, size_t length)
   {
       PosixLogLine *line = &log_lines[stream == ASTRA_LOG_STREAM_STDERR];
       const char *text = bytes;

       for (size_t at = 0u; at < length; ++at) {
           if (text[at] == '\n') {
               log_emit(stream, line);
               continue;
           }
           line->text[line->used++] = text[at];
           line->pending = 1u;
           if (line->used == POSIX_LOG_LINE_MAX)
               log_emit(stream, line);
       }
       return (ssize_t)length;
   }

   void
   astra_posix_log_flush(void)
   {
       for (uint32_t index = 0u; index < 2u; ++index)
           if (log_lines[index].pending)
               log_emit(index == 0u ? ASTRA_LOG_STREAM_STDOUT :
                                      ASTRA_LOG_STREAM_STDERR,
                        &log_lines[index]);
   }
   ```

   `log_emit` with `used == 0` after a newline passes length 0, which
   `astra_log_line` records as an empty line (Task 4).

4. **Dispatch.** `write()`'s LOG dispatch becomes
   `return log_write(slot->value, bytes, length);`.

5. **Flush at exit.** In `_exit` (`:1624`), call `astra_posix_log_flush();`
   before `astra_process_exit`. Declare it in the POSIX layer's internal
   header.

Run the test. Expected: PASS. Then run `make -C sw/userspace/posix test sanitize`
on beast. Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add sw/userspace/posix sw/userspace/runtime
git commit -m "feat(posix): stdout and stderr log per thread, by stream and level"
```

---

### Task 10: `log` command, `logburst` test program, QEMU gate

**Files:**
- Create: `sw/userspace/commands/log/log.c`, `sw/userspace/commands/log/log_filter.c`, `sw/userspace/commands/log/log_filter.h`
- Create: `sw/userspace/commands/logburst/logburst.c`
- Create: `sw/userspace/commands/tests/test_log_filter.c` (follow `test_events_support`)
- Modify: `sw/userspace/commands/Makefile`
- Create: `emu/qemu/test-system-log.py`
- Modify: `~/astra-mg/gates.sh` on beast (add the gate)

**Interfaces:**
- Consumes: `/events/log/syslog` (Task 7), `astra_log_line` (Task 4).
- Produces:
  - `log show [--boot N] [--last DURATION] [--level L] [--program P] [--pid HEX] [--stream S] [--grep TEXT]`
  - `log stream [same filters]`
  - `logburst COUNT [--threads N] [--fault] [--off]`

- [ ] **Step 1: Filter — failing host test**

`log_filter.h`:

```c
typedef struct LogFilter {
    int has_level; uint32_t level;          /* minimum */
    const char *program;                     /* exact ident */
    int has_pid; uint32_t pid;
    const char *stream;                      /* exact */
    const char *grep;                        /* substring of text */
    int has_since; int64_t since_unix_s;     /* --last */
} LogFilter;

/* Parse "10m", "2h", "30s", "1d". 0 on bad input. */
int log_filter_duration(const char *text, int64_t *seconds);
/* Parse "YYYY-MM-DDTHH:MM:SS.mmm+HH:MM" at the line start to Unix seconds. */
int log_filter_line_time(const char *line, int64_t *unix_seconds);
/* 1 when the formatted line passes the filter. */
int log_filter_match(const LogFilter *filter, const char *line);
/* 1 for "--- boot ... ---" lines. */
int log_filter_is_boot_marker(const char *line);
```

`tests/test_log_filter.c`:

```c
#include "../log/log_filter.h"
#include <assert.h>
#include <stdio.h>

static const char line[] =
    "2026-10-05T14:03:22.117-04:00 warning chocolate-doom[10000011] stderr: "
    "MIDI device not found";

int main(void)
{
    LogFilter f = {0};
    int64_t s;

    assert(log_filter_line_time(line, &s) && s == 1791223402);
    assert(log_filter_duration("10m", &s) && s == 600);
    assert(log_filter_duration("2h", &s) && s == 7200);
    assert(!log_filter_duration("10x", &s) && !log_filter_duration("", &s));
    assert(log_filter_match(&f, line));
    f.has_level = 1; f.level = 4; assert(!log_filter_match(&f, line));
    f.level = 3; assert(log_filter_match(&f, line));
    f.program = "chocolate-doom"; assert(log_filter_match(&f, line));
    f.program = "chocolate"; assert(!log_filter_match(&f, line));
    f.program = NULL; f.has_pid = 1; f.pid = 0x10000011u;
    assert(log_filter_match(&f, line));
    f.pid = 0x10000012u; assert(!log_filter_match(&f, line));
    f.has_pid = 0; f.stream = "stderr"; assert(log_filter_match(&f, line));
    f.stream = "stdout"; assert(!log_filter_match(&f, line));
    f.stream = NULL; f.grep = "MIDI"; assert(log_filter_match(&f, line));
    f.grep = "midi"; assert(!log_filter_match(&f, line)); /* case-sensitive */
    f.grep = NULL; f.has_since = 1; f.since_unix_s = 1791223402;
    assert(log_filter_match(&f, line));
    f.since_unix_s = 1791223403; assert(!log_filter_match(&f, line));
    assert(log_filter_is_boot_marker("--- boot 2026-10-05T14:03:20-04:00 build 1a2b3c4d ---"));
    /* A boot-relative line has no wall time: --last cannot exclude it. */
    f.since_unix_s = 1791223403;
    assert(log_filter_match(&f, "boot+12.345 notice x[00000001] log: y"));
    puts("LOG FILTER PASS");
    return 0;
}
```

Register `build/host/test_log_filter` in the commands Makefile's `test:`, in
the same way as `test_events_support` (`Makefile:528-560`). Expected: FAIL.

- [ ] **Step 2: Implement `log_filter.c`**

- `log_filter_line_time` parses the fixed-width fields. It converts the civil
  date to days with the days-from-civil algorithm (Howard Hinnant,
  `days_from_civil`). The result is
  `days*86400 + h*3600 + m*60 + s - offset_seconds`.
- `log_filter_match` splits the line at its first four spaces into time,
  level, `ident[pid]` and `stream:`, then the text. Levels compare by index in
  `{"debug","info","notice","warning","error"}`. The pid is parsed as hex
  between `[` and `]`.

Expected: PASS.

- [ ] **Step 3: `log.c`**

Follow `events/events.c` for the program skeleton and `print_from`/`follow`
(`events.c:121-183`). The program does the following:

- **Arguments.** Parse the subcommand (`show` is the default, or `stream`) and
  the filters.
- **show:** read files oldest first: `/events/log/syslog.3`, `.2`, `.1`, then
  `syslog`. Only `syslog` is served at `EVENTS:log/`, so in phase 1 add
  `syslog.1`..`.3` to the backend's `KIND_LOG_DIR` listing and `describe`, with
  `log_read` taking a file index.
  - Extend Task 7's `log_read` signature to
    `(void *context, uint32_t index, uint64_t offset, ...)`. Update the backend
    test to cover index 1.
  - Split the input into lines and print those that pass `log_filter_match`.
  - `--boot N`: N=0 means lines after the last boot marker; N=-1 means lines
    between the last two markers.
- **stream:** print only lines appended after start. Remember the size of
  `syslog`, then poll as `events --follow` does. When the size shrinks, the
  file rotated: restart at offset 0.
- **Manifest.** `log_LIBRARIES := $(LIBC_LIBRARY) $(RUNTIME_LIBRARY) $(FILESYSTEM_LIBRARY)`,
  `log_EXTRA_SOURCES := log/log_filter.c`. Add `log` to `COMMANDS`.

- [ ] **Step 4: `logburst.c`**

```c
/*
 * Measures what logging costs the program that logs.
 *   logburst COUNT [--threads N] [--fault] [--off]
 * Prints "logburst: COUNT lines, N threads, ELAPSED ns" on stdout.
 * --off formats every line but does not log it: the baseline.
 * --fault logs one line and then faults, for the crash gate.
 */
```

The implementation:

- **Each thread** (pthreads) formats `"burst T I"` with `snprintf` and calls
  `astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG, ...)`, unless
  `--off` is set. It calls `astra_log_line` directly so the result does not
  depend on whether stdout is a terminal.
- **Timing:** `clock_gettime(CLOCK_MONOTONIC)` around the whole run.
- **`--fault`:** `astra_log_line(... "logburst: about to fault" ...)`, then
  `*(volatile int *)0 = 1;`.
- Add `logburst` to `TEST_COMMANDS` (`Makefile:77`).

- [ ] **Step 5: The gate `emu/qemu/test-system-log.py`**

Model it on `test-chocolate-doom.py` (boot, Doom start) and `test-terminal.py`
(typing commands). It takes `qemu rom image`. The image must contain
`ChocolateDoom.app` and the test commands; on beast that is `doom.img` built
with `--test-app`. Check how `images.sh` adds test commands and use an image
that has both.

```python
"""System log gate: docs/SYSTEM_LOG.md section 8."""
import os, re, sys, time
# import terminal_gate / runtime_gate as test-chocolate-doom.py does

LINE = re.compile(
    r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}[+-]\d\d:\d\d|boot\+\d+\.\d{3}) "
    r"(debug|info|notice|warning|error) (\S+)\[([0-9a-f]{8})\] "
    r"(log|stdout|stderr|kernel): (.*)$")

def host_syslog(work):
    return os.path.join(work, "hostfs", ".astra", "syslog")

def lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [l.rstrip("\n") for l in f]

def wait_for(path, predicate, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if os.path.exists(path) and predicate(lines(path)):
            return lines(path)
        time.sleep(0.5)
    raise SystemExit(f"FAIL: {path} never satisfied the check")
```

The checks, each one printing `PASS <name>` or exiting non-zero with `FAIL`:

1. **format:** every non-marker line in the host copy matches `LINE`, and the
   first line is a `--- boot` marker.
2. **doom:** launch Chocolate Doom from the desktop exactly as
   `test-chocolate-doom.py` does. Within 60 s the host copy contains the lines
   below with ident `chocolate-doom`, stream `stdout`, in this order:
   `Z_Init: Init zone memory allocation daemon.` and
   `V_Init: allocate screens.`. Copy the exact strings from Doom's output in
   the existing gate's log if they differ.
3. **burst:** in the terminal, run `logburst 5000 --off`, then
   `logburst 5000`, then `logburst 5000 --threads 4`. Parse the ELAPSED values.
   - Require `logged <= off * 1.05`, and the same bound for the four-thread
     run.
   - Then require that the host copy holds exactly 5,000 lines
     `logburst[...] log: burst 0 I` for I in 0..4999, plus at most one
     `[N lines dropped]` line whose N equals the missing count.
4. **fault:** run `logburst 1 --fault`. The host copy contains
   `logburst: about to fault`.
5. **kernel:** the user-fault report from step 4 is present, with stream
   `kernel` or ident `kernel`.
6. **store copy:** after `machine.close()`, slice the ext4 volume
   (`astra_image.ext4_partition`, `_slice`) and dump `/events/syslog` with
   `debugfs -R "dump /events/syslog OUT"`. Its lines equal the host copy's
   lines.

If the 5% bound fails on the first run, do not loosen it. Profile with
`astra-prof` (see `docs/DEBUGGING.md` §7) and report the cost.

- [ ] **Step 6: Perturbation runs** (each one temporary, then reverted; record the result in the commit message)

1. **Small ring.** Set `ASTRA_LOG_RING_CAPACITY` to 2 and rebuild.
   `logburst 5000` must show a `[N lines dropped]` line, and the gate's burst
   count check must fail with the right N.
2. **No doorbell.** In `maybe_notify`, return immediately. Lines still arrive
   within about 1 s (the sweep). The doom check passes, but slower. Then also
   change the service's deadline from 1 s to forever: the doom check must fail.
   Restore the doorbell with the forever deadline: the doom check passes again.
   That proves the doorbell alone wakes the service.
3. **Service stalled.** Make `events_pump` skip ring draining. The burst
   timing check must still pass while the count check fails with drops. This
   proves logging does not wait on the service.

- [ ] **Step 7: Full verify on beast, then commit**

Add `test-system-log.py` to `~/astra-mg/gates.sh` beside the Doom gate, logging
to `/tmp/mg-system-log.log`. Then run `~/astra-mg/verify-then-publish.sh`.
Expected: `READY-TO-PUBLISH`.

```bash
git add sw/userspace/commands emu/qemu/test-system-log.py
git commit -m "feat(log): log command, burst probe and system log gate"
```

---

### Task 11: Docs and board run

**Files:**
- Modify: `docs/ABI.md`, `docs/MEMORY_MAP_AND_PMMU.md`, `docs/DEBUGGING.md`, `docs/CURRENT_STATE.md`, `docs/SYSTEM_LOG.md`

- [ ] **Step 1: ABI and memory map**

- **`MEMORY_MAP_AND_PMMU.md:54-66`:** add the row
  `| 0x48000000 | 0x48001000 | Kernel time page (read-only) |`, and change the
  reserved row to start at `0x48001000`.
- **`ABI.md`:** add a section "Time page" with `AstraTimePage` and the
  read/retry rule from `time_page.h`. Add a section "Log ring" with the slot
  layout, record header, setup message, and the notify-before-publish rule from
  `log_ring.h` and `docs/SYSTEM_LOG.md` §3.
- **Stale doc:** `ABI.md` still says `LOG_WRITE` needs DEBUG. Correct it to
  match `process.c:10675-10684`.

- [ ] **Step 2: DEBUGGING.md §2**

Rewrite "A program can say something" with these points:

- Use `astra_log_line` or the `astra_log*` calls, or just print. Output goes to
  `/events/syslog` and the host copy.
- Read it with `log show` and `log stream`.
- On the board: `tail -f /var/lib/astra/hostfs/work/.astra/syslog`.
- Under QEMU gates: `<work>/hostfs/.astra/syslog`.
- Remove the claim that the write is gated on `ASTRA_RIGHT_DEBUG`.

- [ ] **Step 3: Board run**

Build the release and install it as `docs/DEBUGGING.md` §7 describes. Launch
Doom over the remote desktop with `de25-rfb.py`. From beast:

```sh
ssh astra-de25 tail -n 60 /var/lib/astra/hostfs/work/.astra/syslog
```

Expected: Doom's banner lines with ident `chocolate-doom`. Record the release
id and two sample lines in `docs/CURRENT_STATE.md`. Also run
`astra-top 10 --json` with Doom idle before and after the release and record
the vCPU percentage, to check whether the time-page update on every switch
measurably costs anything.

- [ ] **Step 4: Spec status**

In `docs/SYSTEM_LOG.md`, change the status line to "phase 1 built
(<commit>), phases 2-4 open".

```bash
git add docs
git commit -m "docs: system log phase 1 -- ABI, debugging, board evidence"
```

---

## Self-Review

**Spec coverage (phase 1 of `docs/SYSTEM_LOG.md` §9):**

| Spec item | Task |
|---|---|
| Time page | 1, 2 |
| Log ring and `astra_log_line` | 3, 4 |
| fd 1/2 log descriptions | 9 |
| `LOG` grant | 8 |
| Drain, file, rotation and host copy | 5, 6, 7 |
| Priority | 7 |
| `log show/stream` | 10 |
| Doom and burst gates | 10 |
| Fault gate | 10 |
| Perturbation | 10 |
| Board | 11 |
| Kernel ring text in the same file | 7, step 3.7 |

Echo to stderr, `syslog()`, SDL_Log and APPL v5 are phase 3. Compression and
retention limits are phase 2. Neither is in this plan, by design.

**Known places where the implementer must confirm a name rather than trust
this plan:**

- `astra_clock_realtime_zone`'s parameter order (Task 1, step 5)
- the area-unmap function's name (Task 4)
- `astra_civil_zone_unpack`'s signature (Task 5)
- the `AstraEventDrained` timestamp field (Task 7)
- `astra_port_receive_from` behaviour on an empty port (Task 7)
- the test helper names in `test_vm.c`/`test_process.c` (Task 2)

Each is called out at its step with the exact grep. These are lookups, not
design decisions.
