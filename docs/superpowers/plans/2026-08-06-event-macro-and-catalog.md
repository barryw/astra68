# Event Macro and Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Plan 2 of six. A message id stops being a bare number. `ASTRA_EVENT`
emits a descriptor — subsystem, level, file, line, format, argument types — into
a section the loader never maps, and compiles a call carrying that descriptor's
address. A build step turns the section into a catalog. Every event knows which
file and line emitted it, and nobody types that anywhere.

**Architecture:** The descriptor is a fixed 128-byte record in `.astra_events`,
a non-loaded section based far from any real address so a message id is
unmistakably a descriptor and never collides with the reserved ids 1–15. The
strings live *inside* the record rather than being pointers into `.rodata`,
which is what makes static context genuinely free: the ROM image strips the
whole section, exactly as it already strips DWARF.

**Tech Stack:** C11 and Python. The macro and its packing are host-tested on the
Mac; the section layout and the ROM budget need the m68k cross-build; the
catalog tool is Python with pytest, which is Mac-only.

## Global Constraints

- Design authority: `docs/superpowers/specs/2026-08-06-event-system-design.md`
  §2 and §3.
- **A disabled level costs one branch.** That is the price of leaving `debug`
  events compiled into shipping code, and it is what makes leaving them in
  correct rather than generous.
- No allocation, no formatting at the point of emission. Formatting is the
  reader's job and the format string never travels.
- Every new behaviour gets a positive and a negative test.

## Where this stops short, on purpose

**u32-class arguments only.** The type enumeration names `u64`, `status`,
`handle`, `s32` and an inline string, and the descriptor records whichever a
call site declared. The macro implements the four-byte ones — `u32`, `s32`,
`status`, `handle` — and refuses `u64` at compile time. Nothing on the machine
needs a 64-bit argument yet, and four four-byte arguments fit the 24-byte
payload with room to spare. `astra_log` keeps the inline-string path it already
has.

**One call site is converted, and only one.** The mechanism needed proving on
the real target, so `bind_standard_assigns` gained an event for the case that
used to say *nothing at all*: a volume that would not take a work directory
left `WORK:` quietly unbound, and the terminal then refused every path a person
typed for a reason nothing on the machine had recorded. `event 0xE0000000` on
the console is strictly more than silence, so this one adds information rather
than degrading any.

**No other call site is converted.** The console renders a typed event as
`event 0x...` because there is no catalog on the machine to render it through —
plan 3 is what fixes that, and converting a call site now would make a
diagnostic that reads clearly today read worse. The mechanism lands here; the
call sites move when they can be read.

**Explicit arities: `ASTRA_EVENT0` … `ASTRA_EVENT4`.** One variadic macro would
need `__VA_OPT__`, which is C2x, or the `##__VA_ARGS__` extension, which
`-pedantic -Werror` refuses. Five macros with a number in the name is the
boring option and it is obvious at three in the morning.

---

### Task 1: The descriptor, and a section the loader never maps

**Files:**
- Create: `sw/include/astra/event_descriptor.h`
- Modify: `sw/userspace/runtime/astra_user.ld`
- Modify: `sw/userspace/supervisor/Makefile` (strip the section from the image)
- Create: `sw/userspace/runtime/tests/test_event_descriptor.c`

**Interfaces:**
- Produces: `AstraEventDescriptor` (128 bytes), `ASTRA_EVENT_DESCRIPTOR_MAGIC`,
  `ASTRA_EVENT_CATALOG_BASE`, the argument type constants, and the
  `.astra_events` section contract.

- [x] **Step 1: Write the failing test**

A host test that the record is exactly what the extractor will expect: 128
bytes, its fields at the offsets the tool reads, and the string fields large
enough for the paths this tree actually has.

- [x] **Step 2: Declare it**

```c
#define ASTRA_EVENT_DESCRIPTOR_MAGIC 0x41455644u /* "AEVD" */
#define ASTRA_EVENT_DESCRIPTOR_SIZE  128u
#define ASTRA_EVENT_FILE_MAX          48u
#define ASTRA_EVENT_FORMAT_MAX        64u

/*
 * Where the descriptors are linked. Nothing is mapped here and nothing is
 * read through it: a message id is a number that happens to be an address, and
 * basing it far from every real one means an id can never be mistaken for a
 * pointer, nor collide with the reserved ids in astra/event.h.
 */
#define ASTRA_EVENT_CATALOG_BASE 0xE0000000u

#define ASTRA_EVENT_ARG_NONE   0u
#define ASTRA_EVENT_ARG_U32    1u
#define ASTRA_EVENT_ARG_S32    2u
#define ASTRA_EVENT_ARG_STATUS 3u
#define ASTRA_EVENT_ARG_HANDLE 4u
#define ASTRA_EVENT_ARG_U64    5u   /* declared; the macro refuses it for now */
#define ASTRA_EVENT_ARG_STRING 6u   /* the inline string; astra_log's path */

typedef struct AstraEventDescriptor {
    uint32_t magic;
    uint16_t line;
    uint8_t  subsystem;
    uint8_t  level;
    uint8_t  argument_count;
    uint8_t  argument_type[4];
    uint8_t  reserved[3];
    char     file[ASTRA_EVENT_FILE_MAX];
    char     format[ASTRA_EVENT_FORMAT_MAX];
} AstraEventDescriptor;
```

The strings are arrays rather than pointers on purpose. A pointer would put the
format in `.rodata`, which is loaded, and the whole claim of §1.2 is that static
context costs zero bytes at runtime.

- [x] **Step 3: Place the section**

In `astra_user.ld`, after `.bss`:

```
    /*
     * The event catalog. Not loaded, not mapped, and based far from every real
     * address: a descriptor's address is only ever used as a message id, and
     * the reader resolves it against the catalog extracted from this section.
     * The ROM image strips it, the way it already strips DWARF.
     */
    . = ASTRA_EVENT_CATALOG_BASE;
    .astra_events (INFO) : {
        __astra_events_start = .;
        KEEP(*(.astra_events))
        __astra_events_end = .;
    }
```

`KEEP` matters: `--gc-sections` is on, and a descriptor referenced only by
having its address taken is exactly the case a collector gets wrong.

In `sw/userspace/supervisor/Makefile`, the image rule strips it:

```make
$(IMAGE): $(TARGET)
	$(OBJCOPY) --strip-debug --remove-section=.astra_events $< $@
```

- [x] **Step 4: Prove it on the cross-build**

`cd sw/userspace && make all`, then check the section exists in the ELF, is not
in any `PT_LOAD`, and is absent from the image:

```sh
m68k-elf-readelf -S build/m68k/astra_supervisor.elf | grep astra_events
m68k-elf-readelf -l build/m68k/astra_supervisor.elf | grep -c astra_events   # 0
m68k-elf-readelf -S build/m68k/astra_supervisor.image.elf | grep -c astra_events
```

- [x] **Step 5: Commit**

---

### Task 2: The macro

**Files:**
- Modify: `sw/include/astra/event.h` (subsystems, the enable table)
- Create: `sw/userspace/runtime/include/astra/event_emit.h`
- Modify: `sw/userspace/runtime/src/log.c` (the enable table's storage)
- Create: `sw/userspace/runtime/tests/test_event_macro.c`

**Interfaces:**
- Produces: `ASTRA_EVENT0` … `ASTRA_EVENT4`, `ASTRA_EVENT_SUBSYSTEM_*`,
  `astra_event_levels[]`, `astra_event_level_set()`.

- [x] **Step 1: Write the failing test**

Against the syscall mock: the message id is the descriptor's address, the level
and flags are what the call site declared, the arguments arrive packed in
declaration order and four bytes each, and **a call below its subsystem's level
issues no syscall at all**. That last one is the whole cost argument and is the
test that matters.

- [x] **Step 2: Write the macro**

```c
#define ASTRA_EVENT_ARGS(...) __VA_ARGS__

#define ASTRA_EVENT2(subsystem, level, format, a0, a1)                        \
    do {                                                                      \
        static const AstraEventDescriptor astra_event_descriptor              \
            __attribute__((section(".astra_events"), used, aligned(4))) = {   \
            ASTRA_EVENT_DESCRIPTOR_MAGIC, (uint16_t)__LINE__,                 \
            (uint8_t)(subsystem), (uint8_t)(level), 2u,                       \
            {ASTRA_EVENT_TYPE_OF(a0), ASTRA_EVENT_TYPE_OF(a1), 0u, 0u},       \
            {0u, 0u, 0u}, __FILE__, format                                    \
        };                                                                    \
        if (astra_event_enabled((subsystem), (level))) {                      \
            uint32_t astra_event_values[2] = {                                \
                ASTRA_EVENT_VALUE_OF(a0), ASTRA_EVENT_VALUE_OF(a1)            \
            };                                                                \
            astra_event_emit_packed(&astra_event_descriptor, (level),         \
                                    astra_event_values, 2u);                  \
        }                                                                     \
    } while (0)
```

`ASTRA_EVENT_TYPE_OF` and `ASTRA_EVENT_VALUE_OF` are `_Generic` selections; a
`uint64_t` argument matches nothing and fails to compile, which is the refusal
this plan wants. `__FILE__` and the format initialise `char[]` members, so the
compiler checks their length against the array and a long one is a build error
rather than a silent truncation.

`astra_event_enabled` is one load, one compare and one branch:

```c
static inline int astra_event_enabled(uint32_t subsystem, uint32_t level)
{
    return level >= astra_event_levels[subsystem];
}
```

- [x] **Step 3: Subsystems**

In `astra/event.h`, a small closed set — `KERNEL`, `STORAGE`, `VFS`, `SHELL`,
`INPUT`, `DISPLAY`, `SUPERVISOR`, `RUNTIME` — and `ASTRA_EVENT_SUBSYSTEM_MAX`.
A closed set because §9's configuration is one level per subsystem and a person
has to be able to read the list.

- [x] **Step 4: Run the tests, host and cross**

- [x] **Step 5: Commit**

---

### Task 3: The catalog

**Files:**
- Create: `tools/event_catalog.py`
- Create: `tools/tests/test_event_catalog.py`

**Interfaces:**
- Produces: `event_catalog.py <elf> -o <catalog.json>` — message id →
  subsystem, level, file, line, format, argument types.

- [x] **Step 1: Write the failing test**

Build a fixture ELF section in the test rather than shelling out to a
cross-compiler: pack a handful of 128-byte records with the struct's layout,
wrap them in a minimal ELF, and assert the tool reads back exactly what went in
— including that a record whose magic is wrong is reported rather than skipped
silently, and that ids are the section's base plus each record's offset.

- [x] **Step 2: Write the tool**

It reads the section header for `.astra_events`, walks it in 128-byte steps,
checks each magic, and emits JSON keyed by the message id in hex. It refuses a
section whose size is not a multiple of the record size — that means the
descriptor changed shape and the tool is reading garbage.

- [x] **Step 3: Run it against the real supervisor**

```sh
python3 tools/event_catalog.py \
    sw/userspace/supervisor/build/m68k/astra_supervisor.elf -o /tmp/catalog.json
```

- [x] **Step 4: Run the pytest half on the Mac**

```sh
python3 -m pytest tools/tests/test_event_catalog.py
```

- [x] **Step 5: Commit**

---

### Task 4: The whole gate

- [x] **Step 1: Beast**

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/boot && make astra_boot.bin
python3 emu/qemu/test-terminal.py ... && python3 emu/qemu/time-boot.py ...
```

Nothing on the machine changed behaviour, so the terminal gate and the boot
budget are regression checks rather than new evidence. The ROM budget is the
one number to watch: the image must not have grown, because the section it
gained is stripped out of it.

- [x] **Step 2: Commit and update the handover**

---

## What this plan deliberately does not do

No call site is converted and the console still cannot render a typed event.
Plan 3 makes the kernel's own trace enum descriptors too, teaches the monitor to
render through the catalog, and moves the call sites once there is something to
read them with.
