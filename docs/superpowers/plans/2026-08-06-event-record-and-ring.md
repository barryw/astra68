# Event Record and Ring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The first of six plans for `2026-08-06-event-system-design.md`. After
this one the machine has a real event record in one ordered stream, every
process may emit into it without holding any capability, and the console is a
sink on that stream rather than a second destination with its own ordering.

**Architecture:** The kernel's existing 64 KiB trace ring gains a user record.
`ASTRA_SYSCALL_LOG_WRITE` stops being a text write gated on `ASTRA_RIGHT_DEBUG`
and becomes an event append — message id, level, typed arguments — gated on
nothing. The right moves to the reading side, where it belongs. `astra_log()`
survives unchanged at its call sites by becoming one reserved message id with an
inline-string argument, so the debug surface does not regress while the typed
path is being built underneath it.

**Tech Stack:** C11. The kernel's host test suite covers all of it; the board
and QEMU confirm nothing regressed. Kernel work, so **nothing here is believed
from the Mac** — every gate is Beast.

## The six plans, so this one's edges make sense

| | Plan | What it lands |
|---|---|---|
| **1** | **this one** | the record, the ring, emitting ungated, reading gated, console as a sink |
| 2 | `ASTRA_EVENT` | the macro, the descriptor section, per-subsystem enable words, the catalog build step |
| 3 | one event model | the kernel's own `KernelTraceEvent` enum becomes descriptors; the monitor renders through the catalog |
| 4 | activity | message headers gain the field; the VFS Kit fills it; a request is one story across four processes |
| 5 | the service | `EVENTS:`, the tiers, the boot ring, coalescing, rate limits, retention |
| 6 | `events` | the command, queries, `--follow`, `--activity` |

Plan 1 is the one everything else needs: until a record exists there is nothing
for a macro to write, nothing for a catalog to describe and nothing for a
service to drain.

## Global Constraints

- Design authority: `docs/superpowers/specs/2026-08-06-event-system-design.md`,
  and `2026-08-06-filesystem-layout-design.md` §6 for the one-stream rule.
- **Emitting may never block and never touches storage.** It is a copy into a
  ring under an interrupt-disabled window, exactly as the ring works today.
- Every new behaviour gets a positive and a negative test. The refusals are the
  substance.
- No allocation anywhere in this plan.
- The ring's torn-read protocol is not modified. Records get bigger; the
  commit-sequence discipline that makes a concurrent reader safe stays exactly
  as it is, because it has tests that would be expensive to re-earn.

## Where this knowingly departs from the spec

**The argument payload is 24 bytes, not 32.** §1.4 allows "at most four
arguments and at most 32 bytes of them", and §1.1 sizes a full record at 64.
A second ring slot is 32 bytes, and 8 of them pay for that slot's own commit
sequence and its discriminator — without which a slot is no longer
self-describing and `kernel_trace_read_slot` cannot answer for a single slot in
isolation, which is the API the monitor and every existing test are built on.

24 bytes holds four `u32`s or three `u64`s. The one combination that no longer
fits is four `u64`s, and plan 2's macro refuses it at compile time rather than
at runtime. The alternative — a third slot for the rare case — buys one argument
combination for a variable-length record in a lock-free ring, and that is a bad
trade at this size.

`docs/superpowers/specs/2026-08-06-event-system-design.md` §1.4 should be
amended to 24 when this lands.

**Severity is ordered here, and that is not a contradiction.**
`2026-08-06-status-and-exit-design.md` forbids ordering *statuses* by severity.
Levels are the other half of that decision: severity lives on the event record,
where it is ordered, comparable and filterable, and it lives there *instead of*
in the status. The two specs agree.

---

### Task 1: A user event in the ring

**Files:**
- Modify: `sw/kernel/trace.h`, `sw/kernel/trace.c`
- Modify: `sw/kernel/tests/test_trace.c`

**Interfaces:**
- Produces: `KernelTraceUserRecord`, `KernelTraceArgumentRecord`,
  `kernel_trace_write_user()`, `kernel_trace_read_user()`.

- [ ] **Step 1: Write the failing test**

In `sw/kernel/tests/test_trace.c`:

```c
static void test_a_user_event_carries_its_arguments(void)
{
    KernelTraceUserRecord user;
    uint8_t payload[KERNEL_TRACE_ARGUMENT_BYTES];
    uint8_t read_back[KERNEL_TRACE_ARGUMENT_BYTES];
    uint32_t length = 0u;

    assert(kernel_trace_init());
    for (uint32_t index = 0u; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)(index + 1u);

    assert(kernel_trace_write_user(0x11223344u, 0x1000u, 7u,
                                   KERNEL_TRACE_LEVEL_WARNING, payload, 12u));
    assert(kernel_trace_read_user(0u, &user, read_back, sizeof(read_back),
                                  &length));
    assert(user.event == KERNEL_TRACE_EVENT_USER);
    assert(user.process == 0x1000u);
    assert(user.message == 0x11223344u);
    assert(user.thread == 7u);
    assert(KERNEL_TRACE_LEVEL_OF(user.flags) == KERNEL_TRACE_LEVEL_WARNING);
    assert(length == 12u);
    assert(memcmp(read_back, payload, 12u) == 0);
    /* Nothing has filled one in yet, and zero is how a reader knows. */
    assert(user.activity == 0u);
}

static void test_an_event_with_no_arguments_costs_one_slot(void)
{
    KernelTraceHeader header;
    KernelTraceUserRecord user;
    uint32_t length = 0u;

    assert(kernel_trace_init());
    assert(kernel_trace_write_user(1u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                   NULL, 0u));
    assert(kernel_trace_header(&header));
    assert(header.write_index == 1u);
    assert(kernel_trace_read_user(0u, &user, NULL, 0u, &length));
    assert(length == 0u);

    /* One with arguments costs two, and they are consecutive. */
    assert(kernel_trace_write_user(2u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                   "ab", 2u));
    assert(kernel_trace_header(&header));
    assert(header.write_index == 3u);
}

static void test_argument_refusals(void)
{
    uint8_t payload[KERNEL_TRACE_ARGUMENT_BYTES + 1u] = {0u};

    assert(kernel_trace_init());
    /* More than the slot holds is refused, never truncated: a truncated
     * argument is a wrong value rather than a missing one. */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                    payload, sizeof(payload)));
    /* A length with no payload, and a payload with no length. */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                    NULL, 4u));
    /* A message id of zero names no message. */
    assert(!kernel_trace_write_user(0u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                    NULL, 0u));
    /* A level outside the five is a caller defect, not a level. */
    assert(!kernel_trace_write_user(1u, 0x1000u, 1u,
                                    KERNEL_TRACE_LEVEL_ERROR + 1u, NULL, 0u));
}

static void test_arguments_lost_to_a_wrap_are_reported_as_lost(void)
{
    KernelTraceUserRecord user;
    uint8_t read_back[KERNEL_TRACE_ARGUMENT_BYTES];
    uint32_t length = 0u;

    /*
     * The header slot survives a wrap that ate its arguments, because the
     * writer reaches the header first. A reader must say the arguments are
     * gone rather than render whatever now occupies the slot: a plausible
     * wrong value in a log is worse than an absent one.
     */
    assert(kernel_trace_init());
    assert(kernel_trace_write_user(1u, 0x1000u, 1u, KERNEL_TRACE_LEVEL_INFO,
                                   "abcd", 4u));
    kernel_trace_test_overwrite_argument_slot(1u);
    assert(!kernel_trace_read_user(0u, &user, read_back, sizeof(read_back),
                                   &length));
}
```

Call all four from `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run on Beast: `cd sw/kernel && make test`
Expected: FAIL — `KernelTraceUserRecord` undeclared.

- [ ] **Step 3: Declare the record**

In `sw/kernel/trace.h`, beside the existing record:

```c
/*
 * User events share the ring with the kernel's own, and are discriminated by
 * the event field rather than by a second ring. One ordered stream with one
 * set of sequence numbers is the whole point -- see the layout spec's section
 * 6 -- and two rings would be two timelines that have to be merged by a
 * reader that cannot know which write happened first.
 *
 * These values sit above the KernelTraceEvent enum's range so that the enum
 * can keep growing. Plan 3 turns the enum itself into descriptors, and then
 * every record in the ring is this shape and the discrimination goes away.
 */
#define KERNEL_TRACE_EVENT_USER           0xE000u
#define KERNEL_TRACE_EVENT_USER_ARGUMENTS 0xE001u

/*
 * Five levels, ordered, because filtering by severity is the one thing a
 * reader of a log always wants. This is where severity lives on this machine;
 * a status never carries it. See 2026-08-06-status-and-exit-design.md.
 */
#define KERNEL_TRACE_LEVEL_DEBUG   0u
#define KERNEL_TRACE_LEVEL_INFO    1u
#define KERNEL_TRACE_LEVEL_NOTICE  2u
#define KERNEL_TRACE_LEVEL_WARNING 3u
#define KERNEL_TRACE_LEVEL_ERROR   4u

#define KERNEL_TRACE_LEVEL_MASK    0x0007u
#define KERNEL_TRACE_LEVEL_OF(flags) ((uint32_t)((flags) & KERNEL_TRACE_LEVEL_MASK))
/* The person was shown this. What makes an event notification history. */
#define KERNEL_TRACE_FLAG_PRESENTED 0x0008u
/* The payload is text rather than typed arguments. */
#define KERNEL_TRACE_FLAG_INLINE_STRING 0x0010u

/*
 * 24 rather than 32: eight bytes of the argument slot pay for its own commit
 * sequence and its discriminator, without which a slot stops being
 * self-describing and kernel_trace_read_slot cannot answer for one in
 * isolation. Four u32 arguments or three u64 ones fit.
 */
#define KERNEL_TRACE_ARGUMENT_BYTES 24u

typedef struct KernelTraceUserRecord {
    uint32_t commit_sequence;
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint16_t event;            /* KERNEL_TRACE_EVENT_USER */
    uint16_t flags;            /* level, presented, inline string */
    uint32_t process;          /* generation-tagged, as OBSERVABILITY requires */
    uint32_t message;          /* the message id; a descriptor from plan 2 on */
    uint32_t activity;         /* zero until plan 4 fills it in */
    uint16_t thread;
    uint16_t payload_length;   /* 0..KERNEL_TRACE_ARGUMENT_BYTES */
} KernelTraceUserRecord;

typedef struct KernelTraceArgumentRecord {
    uint32_t commit_sequence;
    uint16_t event;            /* KERNEL_TRACE_EVENT_USER_ARGUMENTS */
    uint16_t reserved;
    uint8_t  payload[KERNEL_TRACE_ARGUMENT_BYTES];
} KernelTraceArgumentRecord;

/*
 * Appends one event, and its arguments in the following slot when it has any.
 * Refuses rather than truncates: a shortened argument is a wrong value, and a
 * log that carries one is worse than a log that carries none.
 */
bool kernel_trace_write_user(uint32_t message, uint32_t process,
                             uint16_t thread, uint32_t level,
                             const void *payload, uint32_t payload_length);

/*
 * Reads the user event at `slot`. Returns false when the slot is not a user
 * event, when it was torn, or when its arguments were lost to a wrap -- the
 * caller learns nothing rather than something plausible and wrong.
 */
bool kernel_trace_read_user(uint32_t slot, KernelTraceUserRecord *record,
                            void *payload, uint32_t capacity,
                            uint32_t *payload_length);

#if defined(KERNEL_TRACE_HOST_TEST)
void kernel_trace_test_overwrite_argument_slot(uint32_t slot);
#endif
```

Both records must assert their size beside the existing static assertions:

```c
_Static_assert(sizeof(KernelTraceUserRecord) == KERNEL_TRACE_RECORD_SIZE,
               "user trace record must occupy exactly one slot");
_Static_assert(sizeof(KernelTraceArgumentRecord) == KERNEL_TRACE_RECORD_SIZE,
               "argument trace record must occupy exactly one slot");
```

- [ ] **Step 4: Write it**

In `sw/kernel/trace.c`. The header slot is written **first** and the argument
slot second, which is what makes the loss detectable: a wrapping writer reaches
the header before the arguments, so a header that still validates against the
argument slot's expected sequence proves both survived.

The implementation reuses the existing interrupt-disabled window and
commit-sequence discipline rather than introducing a second one. Follow
`kernel_trace_write_at` exactly: disable, check validity, count a drop if the
slot being reused still holds a committed record, zero the commit sequence,
barrier, fill, barrier, commit, barrier, advance the index and the sequence.
Two slots means doing that twice, in order, inside one window — a reader must
never see the header without the arguments having been at least started.

`kernel_trace_read_user` reads the header slot with the same before-and-after
commit check `kernel_trace_read_slot` uses; when `payload_length` is non-zero it
then reads the following slot and requires its `event` to be
`KERNEL_TRACE_EVENT_USER_ARGUMENTS` and its `commit_sequence` to be exactly the
header's plus one. Anything else means the arguments are gone.

- [ ] **Step 5: Run the tests**

Run on Beast: `cd sw/kernel && make test`
Expected: PASS, all suites.

- [ ] **Step 6: Commit**

```bash
git add sw/kernel/trace.h sw/kernel/trace.c sw/kernel/tests/test_trace.c
git commit -m "feat(trace): a user event, and its arguments, in the one ring"
```

---

### Task 2: `LOG_WRITE` becomes an event append, and nothing gates it

**Files:**
- Modify: `sw/include/astra/syscall.h` (the call's shape and its comment)
- Modify: `sw/kernel/process.c` (the handler)
- Modify: `sw/userspace/runtime/src/log.c`, `sw/userspace/runtime/include/astra/runtime.h`
- Modify: `sw/kernel/tests/test_process.c`

**Interfaces:**
- Consumes: `kernel_trace_write_user` from Task 1.
- Produces: `ASTRA_EVENT_MESSAGE_UNSTRUCTURED`, the new call shape, and
  `astra_event_emit()` beside the surviving `astra_log()`.

The call changes shape:

| Register | Was | Is |
|---|---|---|
| `data[1]` | process handle | message id |
| `data[2]` | text pointer | flags: level, presented, inline string |
| `data[3]` | length | argument payload pointer, or 0 |
| `data[4]` | — | payload length, 0..24 |

**The handle goes away entirely.** A process may only speak for itself, which
the old handler enforced by looking a handle up and then checking it named the
caller. With no handle there is nothing to check and nothing to get wrong: the
kernel already knows who is calling.

- [ ] **Step 1: Write the failing tests**

In `sw/kernel/tests/test_process.c`, `test_diagnostic_log_is_gated_and_sanitised`
becomes `test_any_process_may_emit_an_event` and asserts the reversal:

```c
    /*
     * The reversal. Emitting used to need a process handle carrying
     * ASTRA_RIGHT_DEBUG; if the machine's account of what happened depends on
     * a capability, it has holes exactly where something went wrong. A process
     * holding nothing at all can now say what it is doing.
     */
    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = KERNEL_TRACE_LEVEL_WARNING | KERNEL_TRACE_FLAG_INLINE_STRING;
    registers[3] = line_address;
    registers[4] = sizeof(line) - 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_process_stats(&stats));
    assert(stats.diagnostic_logs == 1u);
    assert(stats.diagnostic_log_refusals == 0u);
```

and keeps a refusal for each thing that is still a defect: a zero message id, a
payload longer than the slot, a bad user address, and a level outside the five.
The sanitising test survives, moved to the console sink in Task 3 — the ring
carries the bytes a program wrote, and it is the sink that must not be made to
render them as a cursor movement.

- [ ] **Step 2: Run test to verify it fails**

Run on Beast: `cd sw/kernel && make test`
Expected: FAIL — the handler still demands a handle, so the call is refused
with `ASTRA_SYSCALL_INVALID_HANDLE`.

- [ ] **Step 3: Rewrite the handler**

In `sw/kernel/process.c`, the `ASTRA_SYSCALL_LOG_WRITE` case loses its
`kernel_handle_lookup`, its `target != current` check and its three
handle-shaped refusals, and gains: message id non-zero, level within range,
payload length within `KERNEL_TRACE_ARGUMENT_BYTES`, and the copy from user.
It ends with `kernel_trace_write_user(...)` and the console sink call from
Task 3.

The three counters stay as they are — `diagnostic_logs`,
`diagnostic_log_bytes`, `diagnostic_log_refusals` — because plan 5's rate
limiting is going to need exactly this accounting per process, and renaming
them now would be churn twice.

- [ ] **Step 4: Keep the userspace surface working**

In `sw/userspace/runtime/src/log.c`, `astra_log_write` and `astra_log` keep
their signatures and their call sites, and become one reserved message id:

```c
/*
 * A line of text a program wrote, as an event. The reserved ids are the
 * handful the system needs before any catalog exists; from plan 2 on a message
 * id is the address of a descriptor and these stay reserved below them.
 */
#define ASTRA_EVENT_MESSAGE_UNSTRUCTURED 1u
```

`astra_log_bind` and `astra_log_handle` become no-ops kept for their call sites,
or are removed along with them — the handle they remember is exactly what this
plan deletes. Removing them is the honest option and the call sites are few;
`grep -rn astra_log_bind sw/` names all of them.

- [ ] **Step 5: Run the tests**

Run on Beast:
```sh
cd sw/kernel && make test
cd sw/userspace && make test && make sanitize && make all
```
Expected: PASS throughout.

- [ ] **Step 6: Commit**

```bash
git add sw/include/astra/syscall.h sw/kernel sw/userspace/runtime
git commit -m "feat(events): emitting is universal, because an account with holes is not one"
```

---

### Task 3: Reading is the authority, and the console is a sink

**Files:**
- Modify: `sw/kernel/kernel.c` (`kernel_process_diagnostic_log` becomes the sink)
- Modify: `sw/kernel/monitor.c` (the `trace` command's read path)
- Modify: `sw/kernel/tests/test_process.c`, `sw/kernel/tests/test_monitor.c`

- [ ] **Step 1: Write the failing tests**

Two properties, one test each:

- the console sink renders a user event with the same `[log <pid>] text` shape
  it produces today, and still replaces anything outside `0x20..0x7e` with a
  dot — the sanitising moves here because the console is what a control
  character could dress up as a panic, and the ring is not;
- the sink is off when the build has no debug surface, and the event is still
  in the ring. That is the whole point of the split: a production machine keeps
  its account of itself and simply does not narrate it.

- [ ] **Step 2: Run test to verify it fails**

Run on Beast: `cd sw/kernel && make test`
Expected: FAIL — sanitising still happens in the syscall handler.

- [ ] **Step 3: Move the sanitising to the sink**

`kernel_process_diagnostic_log` takes the record rather than a sanitised
string, renders it when `kernel_process_debug_surface()` is on, and does the
`0x20..0x7e` substitution itself.

- [ ] **Step 4: Gate reading**

The monitor's `trace` command is the only reader that exists today. It gains the
`ASTRA_RIGHT_DEBUG` requirement that the write path just lost — see the events
spec §6.1: logs are where secrets leak, so observation is the privileged half.
A syscall-level read path is plan 6's business; this step is the rule arriving
where the only reader is.

- [ ] **Step 5: The whole gate, on Beast**

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/boot && make astra_boot.bin
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img
python3 emu/qemu/time-boot.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --runs 5 --budget 1.0
```

Expected: every suite green, the terminal gate at six lines, and the boot budget
unmoved. The terminal gate matters more than usual here: `console_shell.c` calls
`astra_log_write` on a refused input read, which is the one call site where this
plan's regression would look exactly like the bug the gate was written for.

- [ ] **Step 6: Commit**

```bash
git add sw/kernel
git commit -m "feat(events): the right moves to reading, and the console becomes a sink"
```

---

## What this plan deliberately does not do

No macro, no descriptors, no catalog: a message id is a bare number until plan 2,
and `astra_log` is the only emitter. That is enough to prove the record, the
ring and the authority reversal, and those three are what every later plan
stands on.

No activity id — the field exists and is zero. No service, no `EVENTS:`, no
tiers, no retention, no coalescing and no rate limiting: the ring is bounded and
drops oldest, which is what it already does, and until something drains it that
is the whole of the story. Plan 5 is where a runaway subsystem stops being able
to evict the failure you were looking for, and it needs a service to do it.
