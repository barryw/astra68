# Namespace Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the three pieces built in `2026-08-06-namespace-foundation.md` to
work. After this plan the machine has no `/`: the shell types `WORK:src/main.c`,
the supervisor binds `SYS:` and `WORK:` when it mounts, and a write through
`SYS:` is refused because the assign carries no write right.

**Architecture:** An assign gains a **root** — where in its mount it begins —
and the Kit gains the one call that turns `NAME:rest` plus a rights mask into
the path the storage protocol already speaks, or into a refusal. The shell keeps
a current assign and a directory under it, and every command routes through that
call with the rights its operation needs. The supervisor, which is the process
that mounts the volume, is where the bindings are made.

**Tech Stack:** C11, freestanding for m68k and hosted for tests. The Kit halves
(Tasks 1 and 2) build and test on the Mac. The shell and the supervisor are
m68k-only and are believed on Beast, through `emu/qemu/test-terminal.py`.

## Global Constraints

- Design authority: `docs/superpowers/specs/2026-08-06-filesystem-layout-design.md`.
  Where this plan and the spec disagree, the spec wins — except for the two
  places named under "Where this knowingly falls short of the spec", which are
  deviations forced by there being one partition and no launch path.
- Assign names are canonicalised uppercase; everything after the colon is
  byte-exact.
- `..` at an assign's root stays an error. Nothing in this plan may add a way
  around it.
- No allocation. Every buffer is caller-owned and bounded, and frames stay small
  — a user thread's stack grows on fault but the shell sits under lwext4, which
  is the deepest call chain in the system.
- Every new behaviour gets a positive and a negative test. The refusals are the
  substance.

## Where this knowingly falls short of the spec

Both are stated here so they are not later mistaken for bugs.

1. **`SYS:` is read-only by right, not by mount.** The spec has `SYS:` on a
   read-only system volume and `WORK:` on a writable state volume. There is one
   ext4 partition. So `SYS:` binds the volume root with `ASTRA_RIGHT_READ` only
   and `WORK:` binds `work/` under it with read and write, and the read-only
   guarantee is the assign's rights rather than the mount's. A program that
   holds only `SYS:` cannot write through it, which is the property the shell
   can demonstrate today; a program that holds `WORK:` can still reach the same
   bytes through it, which a second volume fixes and nothing here does.

2. **`vfs_ext4_backend.c` keeps prefixing its mount point.** The spec's §9 table
   says that prefixing "becomes a bound mount handle". It does not become one in
   this plan and should not: `"/vol/"` is lwext4's own mount namespace and an
   implementation detail of that one backend, and the protocol path the backend
   receives is already volume-absolute. The row means *when there are two
   mounts*; there is one. Changing it now would be churn with a regression risk
   and no property gained.

---

### Task 1: An assign has a root, and one call resolves a path against it

**Files:**
- Modify: `sw/userspace/vfs/include/astra/vfs_assign.h`
- Modify: `sw/userspace/vfs/src/vfs_assign.c`
- Modify: `sw/userspace/vfs/tests/test_vfs_assign.c`
- Modify: `sw/userspace/vfs/Makefile` (the assign test now links the path parser)

**Interfaces:**
- Consumes: `astra_path_split`, `astra_path_normalise` from the foundation plan;
  `ASTRA_RIGHT_READ` / `ASTRA_RIGHT_WRITE` from `astra/syscall.h`.
- Produces:
  - `ASTRA_ASSIGN_ROOT_MAX` (64) and `AstraAssign.root`
  - `astra_assign_bind(table, name, handle, rights, root)` — one argument wider
  - `uint32_t astra_assign_resolve(const AstraAssignTable *table, const char *path, uint32_t rights, char *wire, uint32_t capacity, const AstraAssign **assign)` → `ASTRA_VFS_OK`, `ASTRA_VFS_ERR_INVALID` (not a `NAME:rest` path), `ASTRA_VFS_ERR_NOT_FOUND` (no such assign, or `..` out of one), `ASTRA_VFS_ERR_ACCESS` (rights)

- [x] **Step 1: Write the failing tests**

In `sw/userspace/vfs/tests/test_vfs_assign.c`, every existing `astra_assign_bind`
call gains a root argument — pass `""` for the volume root, so
`astra_assign_bind(&table, "WORK", 7u, 3u)` becomes
`astra_assign_bind(&table, "WORK", 7u, 3u, "")`. Then add:

```c
static void
test_roots_are_normalised(void)
{
    AstraAssignTable table;
    const AstraAssign *found;

    astra_assign_table_init(&table);
    /* A root is stored as the volume-relative path it means, without the
     * leading separator: three spellings, one root. */
    assert(astra_assign_bind(&table, "A", 1u, 1u, "/work") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "B", 1u, 1u, "work/") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "C", 1u, 1u, "/./work") == ASTRA_VFS_OK);
    found = astra_assign_lookup(&table, "A");
    assert(strcmp(found->root, "work") == 0);
    assert(strcmp(astra_assign_lookup(&table, "B")->root, "work") == 0);
    assert(strcmp(astra_assign_lookup(&table, "C")->root, "work") == 0);

    /* The volume root is the empty root, not "/". */
    assert(astra_assign_bind(&table, "D", 1u, 1u, "") == ASTRA_VFS_OK);
    assert(strcmp(astra_assign_lookup(&table, "D")->root, "") == 0);

    /* `..` inside a root is arithmetic and lands at the mount's own root. */
    assert(astra_assign_bind(&table, "F", 1u, 1u, "/work/..") == ASTRA_VFS_OK);
    assert(strcmp(astra_assign_lookup(&table, "F")->root, "") == 0);

    /* A root that climbs out of the mount is not a root. Binding is where
     * authority is handed over, so it is the last place this can be checked. */
    assert(astra_assign_bind(&table, "E", 1u, 1u, "..") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "E", 1u, 1u, NULL) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_lookup(&table, "E") == NULL);
}

static void
test_resolving(void)
{
    AstraAssignTable table;
    const AstraAssign *assign = NULL;
    char wire[64];

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "SYS", 5u, ASTRA_RIGHT_READ, "") ==
           ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "WORK", 5u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "work") ==
           ASTRA_VFS_OK);

    assert(astra_assign_resolve(&table, "WORK:src/main.c", ASTRA_RIGHT_READ,
                                wire, sizeof(wire), &assign) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/work/src/main.c") == 0);
    assert(assign != NULL && assign->handle == 5u);

    /* An assign alone names its own root, with no trailing separator. */
    assert(astra_assign_resolve(&table, "WORK:", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/work") == 0);

    /* An assign rooted at the volume resolves to the volume. */
    assert(astra_assign_resolve(&table, "sys:", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/") == 0);
    assert(astra_assign_resolve(&table, "SYS:commands/ls", ASTRA_RIGHT_READ,
                                wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/commands/ls") == 0);

    /* The rest is normalised on the way through. */
    assert(astra_assign_resolve(&table, "WORK:src/lib/../main.c",
                                ASTRA_RIGHT_READ, wire, sizeof(wire), NULL) ==
           ASTRA_VFS_OK);
    assert(strcmp(wire, "/work/src/main.c") == 0);
}

static void
test_resolution_refusals(void)
{
    AstraAssignTable table;
    char wire[64];
    char tiny[6];

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "SYS", 5u, ASTRA_RIGHT_READ, "") ==
           ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "WORK", 5u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "work") ==
           ASTRA_VFS_OK);

    /* The point of the whole arrangement: SYS: cannot be written through. */
    assert(astra_assign_resolve(&table, "SYS:passwd", ASTRA_RIGHT_WRITE, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_ACCESS);
    assert(astra_assign_resolve(&table, "SYS:passwd",
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_ACCESS);

    /* A name this process was not given cannot be spelled into existence. */
    assert(astra_assign_resolve(&table, "APPS:Editor", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);

    /* Nothing above an assign's root is nameable, from either assign. */
    assert(astra_assign_resolve(&table, "WORK:..", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_assign_resolve(&table, "WORK:../..", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_assign_resolve(&table, "SYS:../etc", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);

    /* Not a path on this machine. */
    assert(astra_assign_resolve(&table, "/etc/passwd", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_resolve(&table, "main.c", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_resolve(NULL, "WORK:x", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);

    /* Truncation would name a different file. */
    assert(astra_assign_resolve(&table, "WORK:src/main.c", ASTRA_RIGHT_READ,
                                tiny, sizeof(tiny), NULL) ==
           ASTRA_VFS_ERR_INVALID);
}
```

Call all three from `main()`.

Every test and every implementation in Tasks 1 and 2 was compiled and run
against the committed `vfs_path.c` under `-fsanitize=address,undefined` while
this plan was written, including the shell's whole chain — qualify, resolve,
and the `cd ..` that walks back to an assign's root and then refuses to leave
it. They pass as written.

- [x] **Step 2: Run test to verify it fails**

In `sw/userspace/vfs/Makefile`, the assign test now needs the path parser:

```make
$(ASSIGN_TEST): src/vfs_assign.c src/vfs_path.c tests/test_vfs_assign.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_assign.c src/vfs_path.c tests/test_vfs_assign.c -o $@
```

Run: `cd sw/userspace/vfs && make test`
Expected: FAIL — `astra_assign_bind` takes four arguments, `astra_assign_resolve`
is undefined, `AstraAssign` has no `root`.

- [x] **Step 3: Widen the header**

In `sw/userspace/vfs/include/astra/vfs_assign.h`, add the include, the bound,
the field, and the call:

```c
#include <astra/vfs_path.h>

/*
 * Where an assign begins inside its mount. Roots are short by nature -- work,
 * apps, sys/commands -- and this is per entry, so the bound is the difference
 * between a namespace that costs one kilobyte and one that costs three.
 */
#define ASTRA_ASSIGN_ROOT_MAX 64u

typedef struct AstraAssign {
    char     name[ASTRA_CAPABILITY_NAME_MAX];  /* canonical uppercase */
    /*
     * Normalised and volume-relative, with no leading separator: "work", or
     * "" for the mount's own root. Stored in the form it is joined in, so
     * resolution is a copy rather than a parse.
     */
    char     root[ASTRA_ASSIGN_ROOT_MAX];
    uint32_t handle;
    uint32_t rights;
} AstraAssign;
```

`astra_assign_bind` gains the root, and resolution appears:

```c
uint32_t astra_assign_bind(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights, const char *root);

/*
 * Turns NAME:rest into the path the storage protocol speaks, or refuses.
 *
 * This is the only place a name becomes a path, and that is why the rights
 * check lives here rather than in each caller: an operation states what it
 * needs, and an assign that was not granted it is refused before anything
 * reaches a disk. ASTRA_VFS_ERR_NOT_FOUND covers both an unbound name and a
 * `..` that would climb out of a bound one, because from inside the namespace
 * those are the same fact -- there is nothing there.
 */
uint32_t astra_assign_resolve(const AstraAssignTable *table, const char *path,
                              uint32_t rights, char *wire, uint32_t capacity,
                              const AstraAssign **assign);
```

- [x] **Step 4: Write the implementation**

In `sw/userspace/vfs/src/vfs_assign.c`, add a bounded copy helper and use it for
both fields, so an entry is written the same way in both branches of `bind`:

```c
static void
copy(char *out, const char *in, uint32_t capacity)
{
    uint32_t index = 0u;

    while (index + 1u < capacity && in[index] != '\0') {
        out[index] = in[index];
        ++index;
    }
    while (index < capacity) {
        out[index++] = '\0';
    }
}
```

`astra_assign_bind` normalises the root through the path parser rather than
growing its own rules — a root is a path, and `..` in one is refused for exactly
the reason it is refused anywhere else:

```c
uint32_t
astra_assign_bind(AstraAssignTable *table, const char *name, uint32_t handle,
                  uint32_t rights, const char *root)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];
    char canonical_root[ASTRA_ASSIGN_ROOT_MAX];

    if (table == NULL || handle == 0u || rights == 0u || root == NULL ||
        !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /*
     * Binding is where authority is handed over, so it is the last place a
     * root may be checked at all: everything downstream trusts it.
     */
    if (astra_path_normalise(root, canonical_root, sizeof(canonical_root)) !=
        ASTRA_VFS_OK) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < table->count; ++index) {
        if (same(table->entries[index].name, canonical_name)) {
            copy(table->entries[index].root, canonical_root,
                 ASTRA_ASSIGN_ROOT_MAX);
            table->entries[index].handle = handle;
            table->entries[index].rights = rights;
            return ASTRA_VFS_OK;
        }
    }
    if (table->count >= ASTRA_ASSIGN_MAX) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    copy(table->entries[table->count].name, canonical_name,
         ASTRA_CAPABILITY_NAME_MAX);
    copy(table->entries[table->count].root, canonical_root,
         ASTRA_ASSIGN_ROOT_MAX);
    table->entries[table->count].handle = handle;
    table->entries[table->count].rights = rights;
    ++table->count;
    return ASTRA_VFS_OK;
}
```

`astra_assign_table_init` clears `root[0]` alongside `name[0]`.

Then resolution:

```c
uint32_t
astra_assign_resolve(const AstraAssignTable *table, const char *path,
                     uint32_t rights, char *wire, uint32_t capacity,
                     const AstraAssign **assign)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    /*
     * The one scratch buffer in the namespace path. It is a frame the deep
     * chain never sees: resolution finishes before the client call it feeds,
     * so this is not paid on top of the service, the backend and lwext4.
     */
    char rest[ASTRA_VFS_PATH_MAX];
    const AstraAssign *found;
    uint32_t length = 0u;
    uint32_t status;

    if (wire == NULL || capacity < 2u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    status = astra_path_split(path, name, sizeof(name), rest, sizeof(rest));
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    found = astra_assign_lookup(table, name);
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if ((found->rights & rights) != rights) {
        return ASTRA_VFS_ERR_ACCESS;
    }

    wire[length++] = '/';
    while (found->root[length - 1u] != '\0') {
        if (length + 1u >= capacity) {
            return ASTRA_VFS_ERR_INVALID;
        }
        wire[length] = found->root[length - 1u];
        ++length;
    }
    if (length > 1u) {
        if (length + 1u >= capacity) {
            return ASTRA_VFS_ERR_INVALID;
        }
        wire[length++] = '/';
    }
    /*
     * Normalised straight into the tail of the caller's buffer. The parser
     * backtracks within the buffer it was given, so it can never walk back
     * over the root: the `..` refusal and the assign boundary are the same
     * mechanism rather than two that have to agree.
     */
    status = astra_path_normalise(rest, wire + length, capacity - length);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    /* An empty rest names the assign's own root, which carries no separator. */
    if (wire[length] == '\0' && length > 1u) {
        wire[length - 1u] = '\0';
    }
    if (assign != NULL) {
        *assign = found;
    }
    return ASTRA_VFS_OK;
}
```

- [x] **Step 5: Run the tests**

Run: `cd sw/userspace/vfs && make test`
Expected: PASS, printing `ASTRA VFS ASSIGN PASS` and `ASTRA VFS PATH PASS`.

- [x] **Step 6: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(vfs): an assign has a root, and one call resolves against it"
```

---

### Task 2: A typed word becomes an absolute path

**Files:**
- Modify: `sw/userspace/vfs/include/astra/vfs_path.h`
- Modify: `sw/userspace/vfs/src/vfs_path.c`
- Modify: `sw/userspace/vfs/tests/test_vfs_path.c`

**Interfaces:**
- Produces: `uint32_t astra_path_qualify(const char *assign, const char *directory, const char *typed, char *out, uint32_t capacity)` → `ASTRA_VFS_OK` or `ASTRA_VFS_ERR_INVALID`.

This is the half of the shell that is worth testing, lifted out of the shell so
that it can be. `console_shell.c` has no host test and cannot get one — it is
a syscall away from a display — so anything in it that can be got wrong belongs
in the Kit instead. What is left in Task 3 is glue.

- [x] **Step 1: Write the failing test**

In `sw/userspace/vfs/tests/test_vfs_path.c`, add:

```c
static void
test_qualifying(void)
{
    char out[64];

    /* A relative word is relative to where the shell is standing. */
    assert(astra_path_qualify("WORK", "src", "main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:src/main.c") == 0);

    assert(astra_path_qualify("WORK", "", "main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:main.c") == 0);

    /* No word at all names where the shell is standing. */
    assert(astra_path_qualify("WORK", "src", NULL, out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:src") == 0);
    assert(astra_path_qualify("WORK", "", "", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:") == 0);

    /* A word carrying an assign is already absolute and is left alone. */
    assert(astra_path_qualify("WORK", "src", "SYS:commands", out,
                              sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "SYS:commands") == 0);

    /* A colon after a separator is a character in a file name, not an
     * assign: only the first component can carry one. */
    assert(astra_path_qualify("WORK", "src", "a/b:c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:src/a/b:c") == 0);

    /* `..` is passed through for the resolver to refuse, not quietly eaten. */
    assert(astra_path_qualify("WORK", "src", "..", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "WORK:src/..") == 0);
}

static void
test_qualify_refusals(void)
{
    char out[64];
    char tiny[8];

    /* A shell with no assign has nowhere to stand and nothing to name. */
    assert(astra_path_qualify("", "", "main.c", out, sizeof(out)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_qualify(NULL, "", "main.c", out, sizeof(out)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_qualify("WORK", NULL, "main.c", out, sizeof(out)) ==
           ASTRA_VFS_ERR_INVALID);
    /* Truncation would name a different file. */
    assert(astra_path_qualify("WORK", "src", "main.c", tiny, sizeof(tiny)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_qualify("WORK", "src", "SYS:a/very/long/one", tiny,
                              sizeof(tiny)) == ASTRA_VFS_ERR_INVALID);
}
```

Call both from `main()`.

- [x] **Step 2: Run test to verify it fails**

Run: `cd sw/userspace/vfs && make test`
Expected: FAIL — `astra_path_qualify` undefined.

- [x] **Step 3: Declare it**

In `sw/userspace/vfs/include/astra/vfs_path.h`:

```c
/*
 * What a word typed at a prompt means, given where the shell is standing.
 *
 * A word whose first component carries a colon is already absolute and is
 * copied; anything else is joined onto the current assign and directory. The
 * result is still only a string -- it is `astra_assign_resolve` that decides
 * whether the process holds what it names.
 */
uint32_t astra_path_qualify(const char *assign, const char *directory,
                            const char *typed, char *out, uint32_t capacity);
```

- [x] **Step 4: Write it**

In `sw/userspace/vfs/src/vfs_path.c`:

```c
/* Appends with truncation refused; returns the new length or `capacity`. */
static uint32_t
append(char *out, uint32_t length, uint32_t capacity, const char *text)
{
    uint32_t index = 0u;

    while (text[index] != '\0') {
        if (length + 1u >= capacity) {
            return capacity;
        }
        out[length++] = text[index++];
    }
    out[length] = '\0';
    return length;
}

/* True when the first component carries a colon, which makes it an assign. */
static int
is_absolute(const char *typed)
{
    uint32_t index = 0u;

    while (typed[index] != '\0' && typed[index] != '/') {
        if (typed[index] == ':') {
            return 1;
        }
        ++index;
    }
    return 0;
}

uint32_t
astra_path_qualify(const char *assign, const char *directory,
                   const char *typed, char *out, uint32_t capacity)
{
    uint32_t length = 0u;

    if (assign == NULL || assign[0] == '\0' || directory == NULL ||
        out == NULL || capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    out[0] = '\0';
    if (typed != NULL && is_absolute(typed)) {
        return append(out, 0u, capacity, typed) == capacity ?
            ASTRA_VFS_ERR_INVALID : ASTRA_VFS_OK;
    }
    length = append(out, length, capacity, assign);
    if (length != capacity) {
        length = append(out, length, capacity, ":");
    }
    if (length != capacity) {
        length = append(out, length, capacity, directory);
    }
    if (length != capacity && typed != NULL && typed[0] != '\0') {
        if (directory[0] != '\0') {
            length = append(out, length, capacity, "/");
        }
        if (length != capacity) {
            length = append(out, length, capacity, typed);
        }
    }
    return length == capacity ? ASTRA_VFS_ERR_INVALID : ASTRA_VFS_OK;
}
```

- [x] **Step 5: Run the tests**

Run: `cd sw/userspace/vfs && make test && make sanitize`
Expected: PASS throughout.

- [x] **Step 6: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(vfs): what a typed word means, given where the shell stands"
```

---

### Task 3: The supervisor binds the assigns and the shell uses them

**Files:**
- Modify: `sw/userspace/supervisor/include/vfs_host.h`, `sw/userspace/supervisor/src/vfs_host.c`
- Modify: `sw/userspace/supervisor/src/console_shell.c`
- Modify: `emu/qemu/test-terminal.py`

**Interfaces:**
- Consumes: everything above.
- Produces: `AstraAssignTable *supervisor_assigns(void)` — the process's
  namespace, or `NULL` before a volume is mounted.

- [x] **Step 1: Bind the namespace where the volume is mounted**

In `vfs_host.h`, declare the accessor and include `<astra/vfs_assign.h>`. In
`vfs_host.c`, add the table and populate it at the end of
`supervisor_vfs_start`, after the client connects:

```c
static AstraAssignTable vfs_assigns;

/*
 * The namespace begins at the process that mounted the volume, which today is
 * this one. The spec has the startup manifest hand these bindings over as
 * capabilities at launch; there is no launch path in userspace yet, so the
 * mounter binds them for itself. When a loader exists this moves and no client
 * of the Kit changes, which is the whole reason resolution lives in the Kit.
 */
static void
bind_standard_assigns(void)
{
    uint32_t status;

    astra_assign_table_init(&vfs_assigns);
    /*
     * One partition, so SYS: is the volume and its read-only-ness is the
     * right it carries rather than the mount it names. See the plan's
     * "where this knowingly falls short of the spec".
     */
    (void)astra_assign_bind(&vfs_assigns, "SYS", vfs_client.session,
                            ASTRA_RIGHT_READ, "");
    /*
     * A volume with no WORK: on it has not been used yet, so making the
     * directory is what installs it. A volume that refuses -- full, or
     * read-only -- boots without WORK: rather than not at all: a binding that
     * cannot be made is omitted, never fatal.
     */
    status = astra_vfs_mkdir(&vfs_client, "/work");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "WORK", vfs_client.session,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "work");
    }
}

AstraAssignTable *
supervisor_assigns(void)
{
    return vfs_ready ? &vfs_assigns : NULL;
}
```

`vfs_ready = 1` moves to immediately after `astra_vfs_connect` succeeds, and
`bind_standard_assigns()` is called after it — the binding uses the client, so
the client has to be usable first.

- [x] **Step 2: The shell stands in an assign**

In `console_shell.c`: `SHELL_PATH_MAX` becomes `ASTRA_VFS_PATH_MAX`, since a
path the protocol will refuse is not worth building, and the state gains the
assign it is standing in:

```c
    char assign[ASTRA_CAPABILITY_NAME_MAX];   /* the current assign, canonical */
    char directory[SHELL_PATH_MAX];           /* normalised, under that assign */
```

`shell_resolve` is replaced entirely — no `/`, no manual joining, and the
operation's rights travel with the request:

```c
/*
 * A word typed at the prompt becomes a wire path, or a refusal that says which
 * kind it was. The rights are the command's own, so a write through a
 * read-only assign is refused here rather than by the disk.
 */
static uint32_t shell_path(const char *name, uint32_t rights, char *wire,
                           uint32_t capacity)
{
    char typed[SHELL_PATH_MAX];
    uint32_t status;

    status = astra_path_qualify(shell.assign, shell.directory, name, typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    return astra_assign_resolve(supervisor_assigns(), typed, rights, wire,
                                capacity, NULL);
}
```

Each command replaces its `shell_resolve` call and its "path too long" line
with the same two lines, differing only in the rights it asks for —
`ASTRA_RIGHT_READ` for `ls`, `cat`; `ASTRA_RIGHT_WRITE` for `mkdir`, `write`,
`rm`:

```c
    status = shell_path(argc > 1 ? argv[1] : NULL, ASTRA_RIGHT_READ, path,
                        sizeof(path));
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
```

`report_status` already prints "access denied" and "not found", so the refusals
arrive at the person in the same vocabulary as every other storage failure and
the shell needs no new strings.

`command_cd` keeps its verify-before-adopt shape and gains nothing else:

```c
static void command_cd(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    char wire[SHELL_PATH_MAX];
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint16_t kind = 0u;
    uint32_t status;

    if (storage() == NULL) {
        write_line("cd: no volume");
        return;
    }
    status = astra_path_qualify(shell.assign, shell.directory,
                                argc > 1 ? argv[1] : NULL, typed,
                                sizeof(typed));
    if (status == ASTRA_VFS_OK && argc < 2) {
        /* `cd` alone goes to the assign's root, not to where it already is. */
        status = astra_path_qualify(shell.assign, "", NULL, typed,
                                    sizeof(typed));
    }
    if (status == ASTRA_VFS_OK) {
        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, wire, sizeof(wire),
                                      NULL);
    }
    if (status != ASTRA_VFS_OK) {
        report_status("cd", status);
        return;
    }
    status = astra_vfs_stat(storage(), wire, NULL, &kind);
    if (status != ASTRA_VFS_OK) {
        report_status("cd", status);
        return;
    }
    if (kind != ASTRA_VFS_KIND_DIRECTORY) {
        write_line("cd: not a directory");
        return;
    }
    /*
     * Adopted only now, and taken apart with the same parser that resolved it
     * -- `wire` is finished with, so it is the scratch the split needs rather
     * than a fourth buffer on a stack that sits under lwext4.
     */
    if (astra_path_split(typed, name, sizeof(name), wire, sizeof(wire)) !=
            ASTRA_VFS_OK ||
        astra_path_normalise(wire, shell.directory,
                             sizeof(shell.directory)) != ASTRA_VFS_OK) {
        shell.directory[0] = '\0';
        write_line("cd: path too long");
        return;
    }
    (void)memcpy(shell.assign, name, sizeof(shell.assign));
}
```

`pwd` and `prompt` print `ASSIGN:directory`, and the startup line names what was
bound rather than a mount point that no longer exists:

```c
static void prompt(void)
{
    astra_terminal_write(&shell.terminal, shell.assign);
    astra_terminal_write(&shell.terminal, ":");
    astra_terminal_write(&shell.terminal, shell.directory);
    astra_terminal_write(&shell.terminal, "> ");
}
```

In `console_shell_run`, after `memset`, the shell stands in the first assign it
was given, preferring the writable one:

```c
    /*
     * A namespace is granted, so the shell starts wherever it was actually
     * given rather than at a root that does not exist. WORK: first because a
     * person's files are what a terminal is for; SYS: is the fallback on a
     * volume that would not take a WORK: directory.
     */
    if (astra_assign_lookup(supervisor_assigns(), "WORK") != NULL) {
        (void)memcpy(shell.assign, "WORK", 5u);
    } else if (astra_assign_lookup(supervisor_assigns(), "SYS") != NULL) {
        (void)memcpy(shell.assign, "SYS", 4u);
    }
```

and the banner becomes `volume_ready ? "namespace: SYS: read-only, WORK:
writable" : "volume: not mounted, file commands will fail"`. `command_help`
gains one line: `paths are ASSIGN:path -- try ls SYS:`.

- [x] **Step 3: Build both ways on the Mac**

Run: `cd sw/userspace && make test && make sanitize && make all`
Expected: PASS, and a supervisor image well inside its 256 KiB reservation —
it was 108 KiB before this plan, and the namespace adds a table of sixteen
92-byte entries in BSS, which the reservation does not count.

- [x] **Step 4: Teach the gate the property this plan exists for**

In `emu/qemu/test-terminal.py`, `SCRIPT` becomes:

```python
SCRIPT = [
    ("mkdir proto", "proto"),
    ("write hello.txt via the protocol", "hello.txt"),
    ("ls", "proto/"),
    ("cat hello.txt", "via the protocol"),
    # The namespace, end to end: SYS: was granted read and nothing else, so
    # the refusal comes from the assign rather than from the filesystem.
    ("write SYS:hello.txt no", "access denied"),
    ("cat SYS:", "is a directory"),
]
```

The first four are unchanged and still pass: they are relative words, and the
shell now stands in `WORK:` instead of at `/`.

- [x] **Step 5: Believe it on Beast**

Everything below is Beast; the Mac cannot run any of it.

```sh
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img
```

Expected: every kernel suite green — the kernel is untouched, so a failure
there means a stale object, not a namespace bug — and the terminal gate passing
all six lines. `make analyze` runs only here: `ANALYZER_CC=gcc` is Apple clang
on the Mac and has no `-fanalyzer`.

If the gate's first `mkdir` fails with "not found", `/work` was not created:
check the boot log for the volume mounting at all, since `bind_standard_assigns`
runs after the mount and a volume that never mounted binds nothing.

- [x] **Step 6: Commit**

```bash
git add sw/userspace/supervisor emu/qemu/test-terminal.py
git commit -m "feat(shell): there is no root, only the assigns you were given"
```

---

## What this plan deliberately does not do

`APPS:`, `CONFIG:`, `TEMP:`, `LIBS:` and the rest of §2 are not bound. Nothing
reads them, and a binding to a directory no program opens is a name with nothing
behind it. Each arrives with the thing that needs it.

The startup manifest, the launch context and the granted-object list in
`AstraStartupInfo` are the next plan and the one after: they need a way to start
a process, which userspace does not have. Until then the supervisor is both the
mounter and the shell, and it binds its own namespace — the arrangement this
plan keeps honest by putting all of the resolution in the Kit, where the
launched process will use exactly the same code.

The events work in `2026-08-06-event-system-design.md` is independent of all of
this and can be done in either order.
