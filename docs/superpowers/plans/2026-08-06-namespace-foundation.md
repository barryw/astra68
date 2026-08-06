# Namespace Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the three pieces every later namespace change depends on — string capability names, the assign table, and assign-rooted path parsing — each with host tests.

**Architecture:** Capability names become bounded strings in the startup ABI. A new assign table in the VFS client Kit maps a canonical uppercase name to a mount handle and rights. A new path parser splits `NAME:rest` and enforces that `..` cannot climb out of an assign. Nothing is wired into the shell or the VFS backend in this plan; that is the next one, and it needs these three to exist first.

**Tech Stack:** C11, freestanding for m68k and hosted for tests. Host tests are `assert`-based C run by `make test`. The Mac can build and run everything in this plan; nothing here needs Beast.

## Global Constraints

- Design authority: `docs/superpowers/specs/2026-08-06-filesystem-layout-design.md`. Where this plan and the spec disagree, the spec wins.
- Assign names are **case-insensitive, canonicalised uppercase**; everything after the colon is **byte-exact** and must never be case-folded (spec §1.3).
- `..` at an assign's root is an **error**, never a parent (spec §1.2).
- Every new behaviour gets a positive and a negative test. Refusals are the substance.
- No allocation: every structure here is caller-owned and bounded.
- Style: match surrounding code — 4-space indent, `astra_`/`Astra` naming, comments that say why rather than what.

---

### Task 1: Capability names become strings

**Files:**
- Modify: `sw/include/astra/process.h`
- Modify: `sw/include/astra/block.h`, `sw/include/astra/display.h`, `sw/include/astra/input.h` (the `ASTRA_CAPABILITY_*` constants)
- Modify: `sw/kernel/process.c` (the bootstrap capability table, near `capability[0].name = ASTRA_CAPABILITY_PROCESS`)
- Modify: `sw/userspace/supervisor/src/supervisor.c`, `sw/userspace/supervisor/src/main.c` (name comparison)
- Test: `sw/userspace/supervisor/tests/test_supervisor.c`, `sw/kernel/tests/test_process.c`

**Interfaces:**
- Produces: `ASTRA_CAPABILITY_NAME_MAX` (16), `AstraStartupCapability.name` as `char[16]`, and `astra_capability_name_equal(const char *a, const char *b)` returning `int`.
- Consumes: nothing.

- [ ] **Step 1: Write the failing test**

In `sw/userspace/supervisor/tests/test_supervisor.c`, add:

```c
static void
test_capability_names_are_bounded_strings(void)
{
    char unterminated[ASTRA_CAPABILITY_NAME_MAX];

    assert(astra_capability_name_equal("PROCESS", "PROCESS"));
    assert(!astra_capability_name_equal("PROCESS", "PROC"));
    assert(!astra_capability_name_equal("PROC", "PROCESS"));
    assert(!astra_capability_name_equal("", "PROCESS"));

    /* A name filling the field with no NUL is not a name. */
    memset(unterminated, 'A', sizeof(unterminated));
    assert(!astra_capability_name_equal(unterminated, "AAAA"));
}
```

Call it from `main()` beside the other tests.

- [ ] **Step 2: Run test to verify it fails**

Run: `cd sw/userspace/supervisor && make test`
Expected: FAIL — `ASTRA_CAPABILITY_NAME_MAX` undeclared, `astra_capability_name_equal` undefined.

- [ ] **Step 3: Widen the ABI**

In `sw/include/astra/process.h`, replace the capability constants and struct:

```c
#define ASTRA_CAPABILITY_NAME_MAX 16u
#define ASTRA_STARTUP_CAPABILITY_SIZE 28u

#define ASTRA_CAPABILITY_PROCESS "PROCESS"
#define ASTRA_CAPABILITY_THREAD  "THREAD"

typedef struct AstraStartupCapability {
    /*
     * A name, not a four-character code. These became the machine's assigns --
     * COMMANDS, DRIVERS, WORK -- and a name a person reads in a manifest may
     * not be limited to what fits in a uint32.
     */
    char     name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t handle;
    uint32_t rights;
    uint32_t flags;
} AstraStartupCapability;

/*
 * Compares two capability names. Bounded on both sides: a field with no NUL in
 * it is not a name, and treating it as one would read past the record.
 */
static inline int
astra_capability_name_equal(const char *left, const char *right)
{
    uint32_t index = 0u;

    if (left == NULL || right == NULL) {
        return 0;
    }
    while (index < ASTRA_CAPABILITY_NAME_MAX) {
        if (left[index] != right[index]) {
            return 0;
        }
        if (left[index] == '\0') {
            return 1;
        }
        ++index;
    }
    return 0;
}
```

In `block.h`, `display.h` and `input.h`, replace the fourcc constants with
strings: `"BLOCK_DEVICE"`, `"BLOCK_IRQ"`, `"DISPLAY"`, `"INPUT"`.

- [ ] **Step 4: Fix every producer and consumer**

In `sw/kernel/process.c`, the bootstrap table assigns names by value today.
Replace each `capability[N].name = ASTRA_CAPABILITY_X;` with a bounded copy:

```c
static void
set_capability_name(AstraStartupCapability *capability, const char *name)
{
    uint32_t index = 0u;

    while (index + 1u < ASTRA_CAPABILITY_NAME_MAX && name[index] != '\0') {
        capability->name[index] = name[index];
        ++index;
    }
    while (index < ASTRA_CAPABILITY_NAME_MAX) {
        capability->name[index++] = '\0';
    }
}
```

In `supervisor.c` and `main.c`, replace `capabilities[index].name == name` and
`capabilities[index].name != name` with `astra_capability_name_equal(...)`.
Their `name` parameters change from `uint32_t` to `const char *`.

- [ ] **Step 5: Run the tests**

Run: `cd sw/userspace && make test`
Expected: PASS, including `SUPERVISOR PASS`.

Run on Beast: `cd sw/kernel && make test`
Expected: PASS — `test_bootstrap_capabilities` still passes with string names.

- [ ] **Step 6: Commit**

```bash
git add sw/include/astra sw/kernel sw/userspace/supervisor
git commit -m "feat(abi): capability names become bounded strings"
```

---

### Task 2: The assign table

**Files:**
- Create: `sw/userspace/vfs/include/astra/vfs_assign.h`
- Create: `sw/userspace/vfs/src/vfs_assign.c`
- Create: `sw/userspace/vfs/tests/test_vfs_assign.c`
- Modify: `sw/userspace/vfs/Makefile`

**Interfaces:**
- Consumes: `ASTRA_CAPABILITY_NAME_MAX` from Task 1.
- Produces:
  - `AstraAssignTable` — caller-owned, `ASTRA_ASSIGN_MAX` (16) entries.
  - `void astra_assign_table_init(AstraAssignTable *table)`
  - `uint32_t astra_assign_bind(AstraAssignTable *table, const char *name, uint32_t handle, uint32_t rights)` → `ASTRA_VFS_OK`, `ASTRA_VFS_ERR_INVALID`, `ASTRA_VFS_ERR_LIMIT`
  - `const AstraAssign *astra_assign_lookup(const AstraAssignTable *table, const char *name)` → entry or `NULL`
  - `uint32_t astra_assign_unbind(AstraAssignTable *table, const char *name)`

- [ ] **Step 1: Write the failing test**

Create `sw/userspace/vfs/tests/test_vfs_assign.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/vfs_assign.h>

static void
test_bind_and_look_up(void)
{
    AstraAssignTable table;
    const AstraAssign *found;

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 7u, 3u) == ASTRA_VFS_OK);

    found = astra_assign_lookup(&table, "WORK");
    assert(found != NULL);
    assert(found->handle == 7u);
    assert(found->rights == 3u);

    /* Case-insensitive: a person typing work: means WORK:. */
    assert(astra_assign_lookup(&table, "work") == found);
    assert(astra_assign_lookup(&table, "Work") == found);

    /* A name nobody bound is not a name this process has. */
    assert(astra_assign_lookup(&table, "SYS") == NULL);
}

static void
test_refusals(void)
{
    AstraAssignTable table;
    char oversized[ASTRA_CAPABILITY_NAME_MAX + 4];

    astra_assign_table_init(&table);
    memset(oversized, 'A', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';

    assert(astra_assign_bind(&table, "", 1u, 1u) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, oversized, 1u, 1u) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "WORK", 0u, 1u) ==
           ASTRA_VFS_ERR_INVALID);   /* no handle is no authority */
    assert(astra_assign_bind(&table, "WORK", 1u, 0u) ==
           ASTRA_VFS_ERR_INVALID);   /* no rights is no authority */
    assert(astra_assign_bind(&table, "WO RK", 1u, 1u) ==
           ASTRA_VFS_ERR_INVALID);   /* a space is not a name */
    assert(astra_assign_bind(&table, "WORK:", 1u, 1u) ==
           ASTRA_VFS_ERR_INVALID);   /* the colon is the separator */
    assert(astra_assign_lookup(&table, "WORK") == NULL);
}

static void
test_rebinding_and_capacity(void)
{
    AstraAssignTable table;
    char name[8];
    uint32_t index;

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 7u, 3u) == ASTRA_VFS_OK);
    /* Rebinding replaces: a name has one meaning at a time. */
    assert(astra_assign_bind(&table, "work", 9u, 1u) == ASTRA_VFS_OK);
    assert(astra_assign_lookup(&table, "WORK")->handle == 9u);

    assert(astra_assign_unbind(&table, "WORK") == ASTRA_VFS_OK);
    assert(astra_assign_lookup(&table, "WORK") == NULL);
    assert(astra_assign_unbind(&table, "WORK") == ASTRA_VFS_ERR_NOT_FOUND);

    for (index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        snprintf(name, sizeof(name), "N%u", index);
        assert(astra_assign_bind(&table, name, index + 1u, 1u) ==
               ASTRA_VFS_OK);
    }
    assert(astra_assign_bind(&table, "ONEMORE", 1u, 1u) ==
           ASTRA_VFS_ERR_LIMIT);
}

int
main(void)
{
    test_bind_and_look_up();
    test_refusals();
    test_rebinding_and_capacity();
    puts("ASTRA VFS ASSIGN PASS");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

In `sw/userspace/vfs/Makefile`, add `src/vfs_assign.c` to `CORE_SOURCES` so the
m68k library carries it, and add this beside the existing `$(HOST_TEST)` rule:

```make
ASSIGN_TEST := build/host/test_vfs_assign

$(ASSIGN_TEST): src/vfs_assign.c tests/test_vfs_assign.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_assign.c tests/test_vfs_assign.c -o $@
```

and change the `test` target to:

```make
test: $(HOST_TEST) $(ASSIGN_TEST)
	./$(HOST_TEST)
	./$(ASSIGN_TEST)
```

Run: `cd sw/userspace/vfs && make test`
Expected: FAIL — `astra/vfs_assign.h` not found.

- [ ] **Step 3: Write the header**

Create `sw/userspace/vfs/include/astra/vfs_assign.h`:

```c
#ifndef ASTRA_VFS_ASSIGN_H
#define ASTRA_VFS_ASSIGN_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

/*
 * A process's namespace: the names it may use, and the authority each one
 * stands for. An assign is not a shortcut to a path -- it is a name for a
 * capability the process was handed, so a name it does not hold cannot be
 * resolved by spelling it correctly.
 *
 * Sixteen entries because that is the startup capability table's own limit;
 * a namespace larger than the grant that seeds it cannot arise.
 */
#define ASTRA_ASSIGN_MAX 16u

typedef struct AstraAssign {
    char     name[ASTRA_CAPABILITY_NAME_MAX];  /* canonical uppercase */
    uint32_t handle;
    uint32_t rights;
} AstraAssign;

typedef struct AstraAssignTable {
    AstraAssign entries[ASTRA_ASSIGN_MAX];
    uint32_t    count;
} AstraAssignTable;

void astra_assign_table_init(AstraAssignTable *table);
uint32_t astra_assign_bind(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights);
const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name);
uint32_t astra_assign_unbind(AstraAssignTable *table, const char *name);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `sw/userspace/vfs/src/vfs_assign.c`:

```c
/*
 * The assign table. Names are canonicalised to uppercase on the way in and
 * compared exactly afterwards, so `work:` and `WORK:` are one binding: assign
 * names are a small closed set typed by people, and a typo must not create a
 * second namespace. Everything after the colon is byte-exact and is not this
 * file's business.
 */

#include <astra/vfs_assign.h>

#include <stddef.h>

static char
upper(char value)
{
    return (value >= 'a' && value <= 'z') ? (char)(value - ('a' - 'A')) : value;
}

/* A name is A-Z, 0-9 and underscore. The colon is the separator and a space
 * makes a name that cannot be typed back. */
static int
name_character(char value)
{
    return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
           value == '_';
}

static int
canonical(const char *name, char *out)
{
    uint32_t index = 0u;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    while (name[index] != '\0') {
        char value;

        if (index + 1u >= ASTRA_CAPABILITY_NAME_MAX) {
            return 0;
        }
        value = upper(name[index]);
        if (!name_character(value)) {
            return 0;
        }
        out[index] = value;
        ++index;
    }
    while (index < ASTRA_CAPABILITY_NAME_MAX) {
        out[index++] = '\0';
    }
    return 1;
}

static int
same(const char *left, const char *right)
{
    for (uint32_t index = 0u; index < ASTRA_CAPABILITY_NAME_MAX; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
        if (left[index] == '\0') {
            return 1;
        }
    }
    return 1;
}

void
astra_assign_table_init(AstraAssignTable *table)
{
    if (table == NULL) {
        return;
    }
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        table->entries[index].name[0] = '\0';
        table->entries[index].handle = 0u;
        table->entries[index].rights = 0u;
    }
    table->count = 0u;
}

uint32_t
astra_assign_bind(AstraAssignTable *table, const char *name, uint32_t handle,
                  uint32_t rights)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];

    if (table == NULL || handle == 0u || rights == 0u ||
        !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < table->count; ++index) {
        if (same(table->entries[index].name, canonical_name)) {
            table->entries[index].handle = handle;
            table->entries[index].rights = rights;
            return ASTRA_VFS_OK;
        }
    }
    if (table->count >= ASTRA_ASSIGN_MAX) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    for (uint32_t index = 0u; index < ASTRA_CAPABILITY_NAME_MAX; ++index) {
        table->entries[table->count].name[index] = canonical_name[index];
    }
    table->entries[table->count].handle = handle;
    table->entries[table->count].rights = rights;
    ++table->count;
    return ASTRA_VFS_OK;
}

const AstraAssign *
astra_assign_lookup(const AstraAssignTable *table, const char *name)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];

    if (table == NULL || !canonical(name, canonical_name)) {
        return NULL;
    }
    for (uint32_t index = 0u; index < table->count; ++index) {
        if (same(table->entries[index].name, canonical_name)) {
            return &table->entries[index];
        }
    }
    return NULL;
}

uint32_t
astra_assign_unbind(AstraAssignTable *table, const char *name)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];

    if (table == NULL || !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < table->count; ++index) {
        if (!same(table->entries[index].name, canonical_name)) {
            continue;
        }
        table->entries[index] = table->entries[table->count - 1u];
        --table->count;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}
```

- [ ] **Step 5: Run the tests**

Run: `cd sw/userspace/vfs && make test`
Expected: PASS, printing `ASTRA VFS ASSIGN PASS`.

- [ ] **Step 6: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(vfs): a process's assign table"
```

---

### Task 3: Assign-rooted path parsing

**Files:**
- Create: `sw/userspace/vfs/include/astra/vfs_path.h`
- Create: `sw/userspace/vfs/src/vfs_path.c`
- Create: `sw/userspace/vfs/tests/test_vfs_path.c`
- Modify: `sw/userspace/vfs/Makefile`

**Interfaces:**
- Consumes: `ASTRA_CAPABILITY_NAME_MAX` from Task 1.
- Produces: `uint32_t astra_path_split(const char *path, char *name, uint32_t name_capacity, char *rest, uint32_t rest_capacity)` returning `ASTRA_VFS_OK` or `ASTRA_VFS_ERR_INVALID`, and `uint32_t astra_path_normalise(const char *rest, char *out, uint32_t capacity)` returning `ASTRA_VFS_OK`, `ASTRA_VFS_ERR_INVALID`, or `ASTRA_VFS_ERR_NOT_FOUND` when `..` would climb out.

- [ ] **Step 1: Write the failing test**

Create `sw/userspace/vfs/tests/test_vfs_path.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/vfs_path.h>

static void
test_splitting(void)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char rest[64];

    assert(astra_path_split("WORK:src/main.c", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_OK);
    assert(strcmp(name, "WORK") == 0);
    assert(strcmp(rest, "src/main.c") == 0);

    /* An assign alone names its root. */
    assert(astra_path_split("SYS:", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_OK);
    assert(strcmp(name, "SYS") == 0);
    assert(strcmp(rest, "") == 0);

    /* The name is canonicalised; the rest is byte-exact. */
    assert(astra_path_split("work:Makefile", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_OK);
    assert(strcmp(name, "WORK") == 0);
    assert(strcmp(rest, "Makefile") == 0);
}

static void
test_refusals(void)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char rest[64];
    char tiny[4];

    /* There is no root: a leading slash is not a path on this machine. */
    assert(astra_path_split("/etc/passwd", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_ERR_INVALID);
    /* A relative path is not absolute and is not this function's business. */
    assert(astra_path_split("src/main.c", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split(":rest", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split("", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split(NULL, name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    /* Truncation would name a different file, so it is refused. */
    assert(astra_path_split("WORK:src/main.c", name, sizeof(name), tiny,
                            sizeof(tiny)) == ASTRA_VFS_ERR_INVALID);
}

static void
test_normalising(void)
{
    char out[64];

    assert(astra_path_normalise("src/./main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("src/lib/../main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("src//main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "") == 0);
}

static void
test_dotdot_cannot_climb_out(void)
{
    char out[64];

    /*
     * The whole security property of an assign-rooted namespace: there is no
     * string a program can build that names something above what it holds.
     */
    assert(astra_path_normalise("..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("src/../..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("../etc", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("src/../../etc", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /* A file legitimately named ".." is not a climb, and neither is "..." */
    assert(astra_path_normalise("...", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "...") == 0);
    assert(astra_path_normalise("..x", out, sizeof(out)) == ASTRA_VFS_OK);
}

int
main(void)
{
    test_splitting();
    test_refusals();
    test_normalising();
    test_dotdot_cannot_climb_out();
    puts("ASTRA VFS PATH PASS");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

In `sw/userspace/vfs/Makefile`, add `src/vfs_path.c` to `CORE_SOURCES`, and:

```make
PATH_TEST := build/host/test_vfs_path

$(PATH_TEST): src/vfs_path.c tests/test_vfs_path.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_path.c tests/test_vfs_path.c -o $@
```

with the `test` target becoming:

```make
test: $(HOST_TEST) $(ASSIGN_TEST) $(PATH_TEST)
	./$(HOST_TEST)
	./$(ASSIGN_TEST)
	./$(PATH_TEST)
```

Run: `cd sw/userspace/vfs && make test`
Expected: FAIL — `astra/vfs_path.h` not found.

- [ ] **Step 3: Write the header**

Create `sw/userspace/vfs/include/astra/vfs_path.h`:

```c
#ifndef ASTRA_VFS_PATH_H
#define ASTRA_VFS_PATH_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

/*
 * Paths on this machine are NAME:rest and there is no root. The absence of one
 * is a security property rather than an aesthetic choice: there is nothing to
 * enumerate, and `..` at an assign's root is an error rather than a parent, so
 * no string a program can build escapes the authority it was given.
 *
 * The assign name is canonicalised to uppercase. Everything after the colon is
 * byte-exact: Makefile and makefile are two files.
 */
uint32_t astra_path_split(const char *path, char *name, uint32_t name_capacity,
                          char *rest, uint32_t rest_capacity);

uint32_t astra_path_normalise(const char *rest, char *out, uint32_t capacity);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `sw/userspace/vfs/src/vfs_path.c`:

```c
#include <astra/vfs_path.h>

#include <stddef.h>

static char
upper(char value)
{
    return (value >= 'a' && value <= 'z') ? (char)(value - ('a' - 'A')) : value;
}

uint32_t
astra_path_split(const char *path, char *name, uint32_t name_capacity,
                 char *rest, uint32_t rest_capacity)
{
    uint32_t index = 0u;
    uint32_t out = 0u;

    if (path == NULL || name == NULL || rest == NULL || name_capacity == 0u ||
        rest_capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    while (path[index] != ':') {
        if (path[index] == '\0' || index + 1u >= name_capacity) {
            return ASTRA_VFS_ERR_INVALID;
        }
        name[index] = upper(path[index]);
        ++index;
    }
    if (index == 0u) {
        return ASTRA_VFS_ERR_INVALID;   /* ":rest" names nothing */
    }
    name[index] = '\0';

    ++index;                            /* step over the colon */
    while (path[index] != '\0') {
        if (out + 1u >= rest_capacity) {
            /* Truncation would name a different file. */
            return ASTRA_VFS_ERR_INVALID;
        }
        rest[out++] = path[index++];
    }
    rest[out] = '\0';
    return ASTRA_VFS_OK;
}

/* True for the component between `start` and `end` being exactly ".." */
static int
is_parent(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 2u && rest[start] == '.' && rest[start + 1u] == '.';
}

static int
is_current(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 1u && rest[start] == '.';
}

uint32_t
astra_path_normalise(const char *rest, char *out, uint32_t capacity)
{
    uint32_t index = 0u;
    uint32_t length = 0u;

    if (rest == NULL || out == NULL || capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    out[0] = '\0';
    while (rest[index] != '\0') {
        uint32_t start;
        uint32_t end;

        while (rest[index] == '/') {
            ++index;                    /* empty components mean nothing */
        }
        start = index;
        while (rest[index] != '\0' && rest[index] != '/') {
            ++index;
        }
        end = index;
        if (end == start) {
            continue;
        }
        if (is_current(rest, start, end)) {
            continue;
        }
        if (is_parent(rest, start, end)) {
            if (length == 0u) {
                /* Above the assign's root there is nothing to name. */
                return ASTRA_VFS_ERR_NOT_FOUND;
            }
            while (length != 0u && out[length - 1u] != '/') {
                --length;
            }
            if (length != 0u) {
                --length;               /* drop the separator too */
            }
            out[length] = '\0';
            continue;
        }
        if (length != 0u) {
            if (length + 1u >= capacity) {
                return ASTRA_VFS_ERR_INVALID;
            }
            out[length++] = '/';
        }
        for (uint32_t at = start; at < end; ++at) {
            if (length + 1u >= capacity) {
                return ASTRA_VFS_ERR_INVALID;
            }
            out[length++] = rest[at];
        }
        out[length] = '\0';
    }
    return ASTRA_VFS_OK;
}
```

- [ ] **Step 5: Run the tests**

Run: `cd sw/userspace/vfs && make test`
Expected: PASS, printing `ASTRA VFS PATH PASS`.

- [ ] **Step 6: Run every host suite that could have been disturbed**

Run: `cd sw/userspace && make test && make sanitize`
Expected: PASS throughout.

- [ ] **Step 7: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(vfs): assign-rooted path parsing, where .. cannot climb out"
```

---

## What this plan deliberately does not do

The shell still builds `/`-rooted paths and the ext4 backend still prefixes a
mount point. Wiring those onto the assign table, and having the supervisor bind
the standard assigns from its capability table, is the next plan — it needs
these three pieces to exist and to be tested first, and it is the plan that
changes behaviour on the machine rather than adding to the Kit.

`APPS:`, `EVENTS:`, the startup manifest and the events service are all later
still, and each is its own plan.
