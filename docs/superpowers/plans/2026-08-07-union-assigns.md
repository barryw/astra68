# Union assigns Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `COMMANDS:` becomes an ordered two-member union that a launched child
can resolve through, which requires a launch grant to carry the root it was
missing.

**Architecture:** A member is a repeated name in the assign table — no member
array, no new struct. `astra_assign_resolve` gains a member index and stays a
pure string operation; the Kit loops the indices and does the trying. Both ABI
records that describe a capability gain a 64-byte root, which fixes the standing
defect where a child's `COMMANDS:` means the whole volume.

**Tech Stack:** C11 freestanding for the kernel and m68k userspace, host C11 for
the unit tests, Python 3 for the QEMU gates. `m68k-elf-gcc` cross toolchain.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-07-union-assigns-design.md`. Where this
  plan and the spec disagree, the spec wins.
- `ASTRA_CAPABILITY_ROOT_MAX` is **64**, and `ASTRA_ASSIGN_ROOT_MAX` is defined
  as that same constant. One number, one `_Static_assert`. Never two.
- `ASTRA_LAUNCH_GRANT_MAX` is **8** after task 2, and
  `KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` is the same number, asserted equal.
- `AstraStartupCapability` and `AstraLaunchGrant` are both **92 bytes** after
  task 1, both `_Static_assert`ed.
- `COMMANDS:` members, in this order: `local/commands` read-write, then
  `commands` read-only. The writable member is first.
- A grant root is normalised, mount-relative, no leading `/`, no `..`
  component, and NUL-terminated within its field. All four are
  `ASTRA_SYSCALL_INVALID_ARGUMENT` at the syscall.
- No throwaway code. Every gate green before any commit that ends a task.
- Build and run everything on `beast`; the Mac cannot build the kernel or the
  m68k user images. Ship with the recipe in `docs/HANDOVER-launch.md` §5.
- Rebuild the boot image after userspace, every time. The ROM carries the user
  image.
- `cd sw/userspace/commands && make` is a **separate step** from
  `sw/userspace && make all`. A stale command image is invisible.
- `cp /tmp/part-clean.img /tmp/part.img` before every terminal-gate run, and
  `pkill -f qemu-system-m68k` after.

---

### Task 1: One root constant, and the two records that carry it

The ABI change and the defect fix. After this task a grant carries a root, the
kernel copies it without reading it, and a child's published capability table
says where in its mount the name begins.

**Files:**
- Modify: `sw/include/astra/process.h` (constants, both structs, one new inline)
- Modify: `sw/kernel/process.h:347-368` (`KernelProcessBootstrapCapability`)
- Modify: `sw/kernel/process.c:2267-2346` (`grant_bootstrap_capabilities`)
- Modify: `sw/kernel/process.c:2404-2467` (`publish_startup_block`)
- Modify: `sw/kernel/process.c:4243-4337` (the `PROCESS_CREATE` syscall)
- Modify: `sw/userspace/vfs/include/astra/vfs_assign.h:33` (`ASTRA_ASSIGN_ROOT_MAX`)
- Test: `sw/kernel/tests/test_process.c` (two new cases)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `#define ASTRA_CAPABILITY_ROOT_MAX 64u`
  - `AstraLaunchGrant.root[ASTRA_CAPABILITY_ROOT_MAX]`,
    `AstraStartupCapability.root[ASTRA_CAPABILITY_ROOT_MAX]`
  - `ASTRA_LAUNCH_GRANT_SIZE 92u`, `ASTRA_STARTUP_CAPABILITY_SIZE 92u`
  - `void astra_capability_root_set(char *field, const char *root)`
  - `KernelProcessBootstrapCapability.root` — a `const char *`, NULL meaning ""

- [ ] **Step 1: Write the failing kernel test for a root that arrives**

Add to `sw/kernel/tests/test_process.c`, immediately after
`test_capability_names_are_bounded_strings`:

```c
/*
 * A grant's root arrives in the child's published record byte for byte.
 *
 * Until this existed a child's COMMANDS: meant the whole volume: the record had
 * nowhere to put a root, so every binding a child made was at its mount's own
 * root and the first program to open a file by name would have read the wrong
 * directory. The kernel carries this field and never reads it, which is the
 * same contract `flags` has.
 */
static void test_capability_roots_are_carried(void)
{
    KernelProcessBootstrapCapability capabilities[1];
    AstraStartupCapability table[4];
    uint32_t process_id = 0u;
    int found = 0;

    loader_build_image();
    initialize_test();
    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = ASTRA_CAPABILITY_BLOCK_DEVICE;
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = ASTRA_DEVICE_ID_BLOCK0;
    capabilities[0].rights = KERNEL_DEVICE_RIGHTS;
    capabilities[0].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE;
    capabilities[0].root = "local/commands";

    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_user_copy_from_asm(
               table, KERNEL_VM_USER_MIN + ASTRA_STARTUP_INFO_SIZE,
               sizeof(table)) == KERNEL_USER_COPY_OK);
    /* The two the kernel installs for itself begin at their mount's root. */
    assert(table[0].root[0] == '\0');
    assert(table[1].root[0] == '\0');
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (astra_capability_name_equal(table[index].name,
                                        ASTRA_CAPABILITY_BLOCK_DEVICE)) {
            assert(strcmp(table[index].root, "local/commands") == 0);
            found = 1;
        }
    }
    assert(found);
}

/* The field is bounded on the way in: a root too long to carry is truncated
 * at the same place on both sides of a launch, never read past. */
static void test_capability_roots_are_bounded(void)
{
    char field[ASTRA_CAPABILITY_ROOT_MAX];
    char oversized[ASTRA_CAPABILITY_ROOT_MAX + 8];

    memset(oversized, 'r', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';

    astra_capability_root_set(field, "work");
    assert(strcmp(field, "work") == 0);
    /* Padded, so a field never carries bytes of the root before it. */
    assert(field[ASTRA_CAPABILITY_ROOT_MAX - 1u] == '\0');

    astra_capability_root_set(field, NULL);
    assert(field[0] == '\0');

    astra_capability_root_set(field, oversized);
    assert(field[ASTRA_CAPABILITY_ROOT_MAX - 1u] == '\0');
    assert(strlen(field) == ASTRA_CAPABILITY_ROOT_MAX - 1u);
}
```

and call both from `main`, beside
`test_capability_names_are_bounded_strings();`:

```c
    test_capability_roots_are_carried();
    test_capability_roots_are_bounded();
```

- [ ] **Step 2: Run the test to verify it fails**

Run on `beast`:

```sh
cd sw/kernel && make test 2>&1 | tail -20
```

Expected: a compile failure, `'KernelProcessBootstrapCapability' has no member
named 'root'`.

- [ ] **Step 3: Add the constant, the field and the setter**

In `sw/include/astra/process.h`, beside the other size constants at the top:

```c
#define ASTRA_STARTUP_CAPABILITY_SIZE 92u
/*
 * Where a granted name begins inside its mount, and the one number for it.
 *
 * A grant that could not say this made every binding a child built land at the
 * mount's own root, so a child holding COMMANDS: was holding the whole volume.
 * Sixty-four bytes because that is what an assign's own root costs; a shorter
 * field here would be a second limit, a truncation rule, and an explanation
 * that never ends.
 */
#define ASTRA_CAPABILITY_ROOT_MAX 64u
```

Add the field to both records, last so the existing offsets do not move:

```c
typedef struct AstraLaunchGrant {
    char     name[ASTRA_CAPABILITY_NAME_MAX];  /* what the child calls it */
    uint32_t handle;                           /* the caller's own handle */
    uint32_t rights;                           /* a subset of what it holds */
    uint32_t flags;
    /*
     * Normalised, mount-relative, no leading separator: "commands", or "" for
     * the mount's own root. Carried by the kernel and never read by it, the
     * same contract `flags` has -- what a name means is the launcher's
     * statement to the child.
     */
    char     root[ASTRA_CAPABILITY_ROOT_MAX];
} AstraLaunchGrant;

#define ASTRA_LAUNCH_GRANT_SIZE 92u
```

and the same `root` field, with the same comment shortened to one line, at the
end of `AstraStartupCapability`.

Beside `astra_capability_name_set`, add:

```c
/*
 * Copies and pads a root into a bounded ABI field. NULL is the empty root,
 * which is what the firmware's own grants carry: they name devices, not places
 * in a filesystem, and a NULL here would otherwise be a special case at every
 * call site instead of one.
 */
static inline void
astra_capability_root_set(char *field, const char *root)
{
    uint32_t index = 0u;

    if (field == NULL) {
        return;
    }
    while (root != NULL && index + 1u < ASTRA_CAPABILITY_ROOT_MAX &&
           root[index] != '\0') {
        field[index] = root[index];
        ++index;
    }
    while (index < ASTRA_CAPABILITY_ROOT_MAX) {
        field[index++] = '\0';
    }
}
```

In `sw/userspace/vfs/include/astra/vfs_assign.h`, replace the
`ASTRA_ASSIGN_ROOT_MAX` definition with the one number:

```c
/*
 * Where an assign begins inside its mount. This is the grant's root field and
 * nothing else: two constants for one limit is the mistake that cost four
 * tasks the last time, so there is one.
 */
#define ASTRA_ASSIGN_ROOT_MAX ASTRA_CAPABILITY_ROOT_MAX
```

- [ ] **Step 4: Carry the root through the kernel**

In `sw/kernel/process.h`, add to `KernelProcessBootstrapCapability` after
`flags`:

```c
    /*
     * Where the name begins inside its mount, or NULL for the mount's own
     * root. A pointer for the same reason `name` is one: the firmware builds
     * these from constants, and the copy into the bounded ABI field happens
     * where the startup block is written.
     */
    const char *root;
```

In `grant_bootstrap_capabilities`, beside the `flags` line:

```c
        granted[index].flags = entry->flags;
        astra_capability_root_set(granted[index].root, entry->root);
```

In the `ASTRA_SYSCALL_PROCESS_CREATE` case, validate the root and point at it.
The record was copied into the kernel's own `grants[]`, so the bytes are the
kernel's; what has to be checked is that they form a string it may pass on.
Add inside the per-grant loop, before `requested[index].name = names[index];`:

```c
            /*
             * The root is validated in place rather than copied out: `grants`
             * is the kernel's own copy already, so a field with a NUL in it is
             * a C string here. A field without one is not a root and is
             * refused -- carrying it would read past the record.
             *
             * A leading separator or a `..` component is refused for a
             * different reason: this is where a root enters the system from
             * outside, and a root that climbs is not a root. Resolution's own
             * `..` rule is unaffected and still runs later.
             */
            {
                uint32_t at;
                int terminated = 0;

                for (at = 0u; at < ASTRA_CAPABILITY_ROOT_MAX; ++at) {
                    if (grants[index].root[at] == '\0') {
                        terminated = 1;
                        break;
                    }
                }
                if (!terminated || grants[index].root[0] == '/') {
                    result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                    break;
                }
                for (at = 0u; at + 1u < ASTRA_CAPABILITY_ROOT_MAX &&
                                grants[index].root[at] != '\0'; ++at) {
                    if (grants[index].root[at] != '.' ||
                        grants[index].root[at + 1u] != '.')
                        continue;
                    if ((at == 0u || grants[index].root[at - 1u] == '/') &&
                        (grants[index].root[at + 2u] == '\0' ||
                         grants[index].root[at + 2u] == '/')) {
                        result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                        break;
                    }
                }
                if (result != ASTRA_SYSCALL_OK)
                    break;
            }
            requested[index].root = grants[index].root;
```

- [ ] **Step 5: Build the startup block without a second copy of it**

`publish_startup_block` holds `capability[STARTUP_CAPABILITY_TOTAL_MAX]` and
then copies it into `page`. At 92 bytes a record that array is 920 bytes of a
supervisor stack that is 8 KiB
(`KERNEL_THREAD_SUPERVISOR_STACK_SIZE 0x00002000`), on a path that already
nests the syscall frame under it. Build into the page instead and the array
goes away.

In `sw/kernel/process.c`, replace the local declaration and the two copies:

```c
    uint8_t page[ASTRA_STARTUP_INFO_SIZE +
                 (STARTUP_CAPABILITY_TOTAL_MAX *
                  ASTRA_STARTUP_CAPABILITY_SIZE) +
                 (ASTRA_LAUNCH_ARGUMENT_MAX * 4u) +
                 ASTRA_LAUNCH_ARGUMENT_BYTES];
    AstraStartupInfo info;
    /*
     * Written straight into the page. A second array of these used to sit on
     * this frame and be copied in whole; at 92 bytes a record that is 920
     * bytes of an 8 KiB supervisor stack, under a syscall frame that is now
     * carrying eight grants of its own.
     */
    AstraStartupCapability *capability =
        (AstraStartupCapability *)(void *)(page + ASTRA_STARTUP_INFO_SIZE);
```

then delete `kernel_bytes_clear(capability, sizeof(capability));`, move
`kernel_bytes_clear(page, sizeof(page));` to before the first use of
`capability`, and delete the

```c
    kernel_bytes_copy(page + ASTRA_STARTUP_INFO_SIZE, capability,
                      count * ASTRA_STARTUP_CAPABILITY_SIZE);
```

line. The `kernel_bytes_copy(&capability[2], bootstrap, ...)` line stays as it
is and now writes into the page directly.

- [ ] **Step 6: Run the kernel tests**

```sh
cd sw/kernel && make test 2>&1 | tail -20
```

Expected: `KERNEL PROCESS PASS` and every other suite's PASS line, no
assertion failures.

- [ ] **Step 7: Check the ROM and the stack still fit**

```sh
cd sw/kernel && make && make clean && make K1_QUALIFICATION=1
cd sw/boot && make astra_boot.bin && ls -l astra_boot.bin
```

Expected: both kernels build with no `-Werror` failure and no linker overflow;
`astra_boot.bin` is produced. Record its size in the commit message — the
startup block grew and the ROM budget in `docs/MEMORY_MAP.md` is the thing that
notices.

- [ ] **Step 8: Commit**

```bash
git add sw/include/astra/process.h sw/kernel/process.h sw/kernel/process.c \
        sw/kernel/tests/test_process.c \
        sw/userspace/vfs/include/astra/vfs_assign.h
git commit -m "feat(abi): a grant says where in its mount the name begins"
```

---

### Task 2: Eight grants, one number

**Files:**
- Modify: `sw/include/astra/process.h:31` (`ASTRA_LAUNCH_GRANT_MAX`)
- Modify: `sw/kernel/process.h` (`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX`
  and the `_Static_assert` that ties them)
- Test: `sw/kernel/tests/test_process.c:6749` (the existing over-the-ceiling case)

**Interfaces:**
- Consumes: task 1's records.
- Produces: `ASTRA_LAUNCH_GRANT_MAX == 8`, equal to
  `KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX`.

- [ ] **Step 1: Write the failing test**

Add to `sw/kernel/tests/test_process.c` inside
`test_capability_roots_are_bounded`, at the end:

```c
    /*
     * One number, checked here as well as asserted at compile time. A launch
     * of more than the loader's ceiling used to fail with INVALID_ARGUMENT
     * from inside the loader, naming neither the grant nor the reason, and it
     * stayed latent for four tasks.
     */
    assert(ASTRA_LAUNCH_GRANT_MAX == 8u);
    assert(ASTRA_LAUNCH_GRANT_MAX == KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX);
```

- [ ] **Step 2: Run it to verify it fails**

```sh
cd sw/kernel && make test 2>&1 | tail -20
```

Expected: `Assertion 'ASTRA_LAUNCH_GRANT_MAX == 8u' failed`.

- [ ] **Step 3: Move the number**

In `sw/include/astra/process.h`:

```c
/*
 * Eight, because a shell hands a child three streams, its namespace, and a
 * COMMANDS: that has two members. Six was exactly what a single-member
 * namespace needed, which is how a ceiling becomes a surprise.
 */
#define ASTRA_LAUNCH_GRANT_MAX 8u
```

In `sw/kernel/process.h`, set `KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` to `8u`
and confirm the existing `_Static_assert` tying it to `ASTRA_LAUNCH_GRANT_MAX`
is still there; if it is not, add it beside the definition:

```c
_Static_assert(KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX ==
                   ASTRA_LAUNCH_GRANT_MAX,
               "one limit, one number: a loader ceiling below the ABI's is a "
               "refusal that names neither the grant nor the reason");
```

- [ ] **Step 4: Run the tests**

```sh
cd sw/kernel && make test 2>&1 | tail -20
cd sw/kernel && make && make clean && make K1_QUALIFICATION=1
```

Expected: every suite PASS; both kernels link.

- [ ] **Step 5: Commit**

```bash
git add sw/include/astra/process.h sw/kernel/process.h \
        sw/kernel/tests/test_process.c
git commit -m "feat(launch): eight grants, because a union needs a seventh"
```

---

### Task 3: A member is a repeated name

Pure, host-tested, no filesystem anywhere near it.

**Files:**
- Modify: `sw/userspace/vfs/include/astra/vfs_assign.h`
- Modify: `sw/userspace/vfs/src/vfs_assign.c`
- Modify: `sw/userspace/vfs/tests/test_vfs_assign.c`
- Modify (mechanical, `0u` at every call): `sw/userspace/supervisor/src/console_shell.c:123,292,546,569`,
  `sw/userspace/supervisor/src/events_host.c:93`,
  `sw/userspace/commands/events/events.c:432`

**Interfaces:**
- Consumes: `ASTRA_ASSIGN_ROOT_MAX` from task 1.
- Produces:
  - `uint32_t astra_assign_join(AstraAssignTable *, const char *name, uint32_t handle, uint32_t rights, const char *root)`
  - `const AstraAssign *astra_assign_member(const AstraAssignTable *, const char *name, uint32_t member)`
  - `astra_assign_resolve` with a new fourth parameter, `uint32_t member`,
    before `wire`.

- [ ] **Step 1: Write the failing tests**

Add to `sw/userspace/vfs/tests/test_vfs_assign.c`, before `main`:

```c
static void
test_joining_makes_members(void)
{
    AstraAssignTable table;
    const AstraAssign *member;
    char wire[ASTRA_VFS_PATH_MAX];

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "COMMANDS", 9u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "local/commands") == ASTRA_VFS_OK);
    assert(astra_assign_join(&table, "COMMANDS", 9u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_OK);
    assert(table.count == 2u);

    /* Order is join order, and lookup is member zero. */
    member = astra_assign_member(&table, "COMMANDS", 0u);
    assert(member == astra_assign_lookup(&table, "COMMANDS"));
    assert(strcmp(member->root, "local/commands") == 0);
    member = astra_assign_member(&table, "COMMANDS", 1u);
    assert(strcmp(member->root, "commands") == 0);
    /* Past the last member there is nothing, which is what ends a loop. */
    assert(astra_assign_member(&table, "COMMANDS", 2u) == NULL);

    /* Resolution answers per member. */
    assert(astra_assign_resolve(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                0u, wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/local/commands/status") == 0);
    assert(astra_assign_resolve(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                1u, wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/commands/status") == 0);
    assert(astra_assign_resolve(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                2u, wire, sizeof(wire), NULL) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /*
     * Rights are per member. A caller looping for write skips the read-only
     * member and lands on the first writable one, which is what "creation goes
     * to the primary" is -- a consequence of a fixed order rather than a
     * stored field.
     */
    assert(astra_assign_resolve(&table, "COMMANDS:new", ASTRA_RIGHT_WRITE, 0u,
                                wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(astra_assign_resolve(&table, "COMMANDS:new", ASTRA_RIGHT_WRITE, 1u,
                                wire, sizeof(wire), NULL) ==
           ASTRA_VFS_ERR_ACCESS);
}

static void
test_joining_refusals(void)
{
    AstraAssignTable table;

    astra_assign_table_init(&table);
    /* Joining is not a way to create a binding. */
    assert(astra_assign_join(&table, "COMMANDS", 1u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_ERR_NOT_FOUND);
    assert(table.count == 0u);

    assert(astra_assign_bind(&table, "COMMANDS", 1u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_OK);
    assert(astra_assign_join(&table, "COMMANDS", 0u, ASTRA_RIGHT_READ,
                             "x") == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_join(&table, "COMMANDS", 1u, 0u, "x") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_join(&table, "COMMANDS", 1u, ASTRA_RIGHT_READ,
                             "../etc") == ASTRA_VFS_ERR_INVALID);
    assert(table.count == 1u);

    /* Binding replaces every member: a name has one meaning at a time. */
    assert(astra_assign_join(&table, "COMMANDS", 2u, ASTRA_RIGHT_READ,
                             "other") == ASTRA_VFS_OK);
    assert(table.count == 2u);
    assert(astra_assign_bind(&table, "COMMANDS", 3u, ASTRA_RIGHT_READ,
                             "only") == ASTRA_VFS_OK);
    assert(table.count == 1u);
    assert(astra_assign_member(&table, "COMMANDS", 1u) == NULL);
    assert(astra_assign_lookup(&table, "COMMANDS")->handle == 3u);
}

static void
test_unbinding_a_union_keeps_the_order_of_the_rest(void)
{
    AstraAssignTable table;

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 1u, ASTRA_RIGHT_READ, "work") ==
           ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "COMMANDS", 2u, ASTRA_RIGHT_READ,
                             "local/commands") == ASTRA_VFS_OK);
    assert(astra_assign_join(&table, "COMMANDS", 3u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "SYS", 4u, ASTRA_RIGHT_READ, "") ==
           ASTRA_VFS_OK);

    /* Removing a name removes every member of it, and nothing else moves. */
    assert(astra_assign_unbind(&table, "WORK") == ASTRA_VFS_OK);
    assert(table.count == 3u);
    assert(astra_assign_member(&table, "COMMANDS", 0u)->handle == 2u);
    assert(astra_assign_member(&table, "COMMANDS", 1u)->handle == 3u);
    assert(astra_assign_lookup(&table, "SYS")->handle == 4u);

    assert(astra_assign_unbind(&table, "COMMANDS") == ASTRA_VFS_OK);
    assert(table.count == 1u);
    assert(astra_assign_lookup(&table, "COMMANDS") == NULL);
    assert(astra_assign_lookup(&table, "SYS")->handle == 4u);
}
```

Call all three from `main`, after `test_unbinding_keeps_the_rest_reachable();`:

```c
    test_joining_makes_members();
    test_joining_refusals();
    test_unbinding_a_union_keeps_the_order_of_the_rest();
```

Then add the member argument to every existing `astra_assign_resolve` call in
this file — `0u` immediately after the rights argument, at lines 176, 182, 187,
190, 195, 201, 204, 224, 226, 231, 235, 237, 239, 242, 246, 248, 250, 253, 257
and 260.

- [ ] **Step 2: Run the test to verify it fails**

```sh
cd sw/userspace/vfs && make test 2>&1 | tail -20
```

Expected: a compile failure, `implicit declaration of function
'astra_assign_join'`.

- [ ] **Step 3: Declare the two new functions and the new parameter**

In `sw/userspace/vfs/include/astra/vfs_assign.h`, after `astra_assign_bind`:

```c
/*
 * Appends one member to a name that already exists. Order is join order and
 * nothing else, and it is the order lookup tries.
 *
 * A name that is not bound is ASTRA_VFS_ERR_NOT_FOUND: joining is not a way to
 * create a binding, because a member joined to nothing would be a name whose
 * first member is an accident of ordering. Everything else it refuses,
 * `astra_assign_bind` refuses for the same reasons.
 */
uint32_t astra_assign_join(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights, const char *root);

/*
 * The member'th binding of a name, or NULL once the index passes the last one
 * -- which is what ends a caller's loop. `astra_assign_lookup` is member zero.
 */
const AstraAssign *astra_assign_member(const AstraAssignTable *table,
                                       const char *name, uint32_t member);
```

Change the `astra_assign_resolve` declaration to carry the index, and extend
its comment:

```c
/*
 * Turns NAME:rest into the path the storage protocol speaks, or refuses.
 *
 * ... existing text ...
 *
 * `member` selects which member of a union answers, and passing the count of
 * members is ASTRA_VFS_ERR_NOT_FOUND rather than a failure: that is how a
 * caller's loop ends. Resolution does no I/O and never will -- the tempting
 * implementation is to stat each member until one answers, and that drags the
 * disk into the one layer whose value is having none. The Kit does the trying.
 */
uint32_t astra_assign_resolve(const AstraAssignTable *table, const char *path,
                              uint32_t rights, uint32_t member, char *wire,
                              uint32_t capacity, const AstraAssign **assign);
```

Update the `ASTRA_ASSIGN_MAX` comment to say what the number now bounds:

```c
/*
 * Sixteen entries, because that is the startup capability table's own limit: a
 * namespace larger than the grant that seeds it cannot arise. It counts
 * *members* rather than names -- a member is a binding, and a union is two
 * bindings that share a name.
 */
#define ASTRA_ASSIGN_MAX 16u
```

- [ ] **Step 4: Implement**

In `sw/userspace/vfs/src/vfs_assign.c`, replace the body of `astra_assign_bind`'s
existing-name branch so it drops every member rather than editing the first,
and add the two new functions.

`astra_assign_bind`, replacing the `for` loop that finds an existing name:

```c
    /*
     * A name has one meaning at a time, and that meaning may now be a list --
     * so binding drops every member of the name before it makes the new one.
     * Editing the first member in place would leave a union whose head a
     * caller replaced and whose tail it never mentioned.
     */
    index = 0u;
    while (index < table->count) {
        if (!same(table->entries[index].name, canonical_name)) {
            ++index;
            continue;
        }
        for (uint32_t at = index; at + 1u < table->count; ++at) {
            table->entries[at] = table->entries[at + 1u];
        }
        --table->count;
    }
    if (table->count >= ASTRA_ASSIGN_MAX) {
        return ASTRA_VFS_ERR_LIMIT;
    }
```

then the existing append block, unchanged.

New functions, after `astra_assign_bind`:

```c
uint32_t
astra_assign_join(AstraAssignTable *table, const char *name, uint32_t handle,
                  uint32_t rights, const char *root)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];
    char canonical_root[ASTRA_ASSIGN_ROOT_MAX];

    if (table == NULL || handle == 0u || rights == 0u || root == NULL ||
        !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (astra_path_normalise(root, canonical_root, sizeof(canonical_root)) !=
        ASTRA_VFS_OK) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (astra_assign_member(table, canonical_name, 0u) == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
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

const AstraAssign *
astra_assign_member(const AstraAssignTable *table, const char *name,
                    uint32_t member)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t index;
    uint32_t seen = 0u;

    if (table == NULL || !canonical(name, canonical_name)) {
        return NULL;
    }
    for (index = 0u; index < table->count; ++index) {
        if (!same(table->entries[index].name, canonical_name)) {
            continue;
        }
        if (seen == member) {
            return &table->entries[index];
        }
        ++seen;
    }
    return NULL;
}
```

Replace `astra_assign_lookup`'s body with:

```c
const AstraAssign *
astra_assign_lookup(const AstraAssignTable *table, const char *name)
{
    return astra_assign_member(table, name, 0u);
}
```

Replace `astra_assign_unbind`'s body so it removes every member and keeps the
order of what is left:

```c
uint32_t
astra_assign_unbind(AstraAssignTable *table, const char *name)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t index = 0u;
    uint32_t removed = 0u;

    if (table == NULL || !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /*
     * Every member goes, and the rest shift down. The last entry used to be
     * moved into the hole, on the reasoning that order is not a property of a
     * namespace. It is one now: a member's position is the order it is tried
     * in, so compacting by swap would silently reorder somebody else's union.
     */
    while (index < table->count) {
        if (!same(table->entries[index].name, canonical_name)) {
            ++index;
            continue;
        }
        for (uint32_t at = index; at + 1u < table->count; ++at) {
            table->entries[at] = table->entries[at + 1u];
        }
        --table->count;
        ++removed;
    }
    return removed != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
}
```

In `astra_assign_resolve`, take the new parameter and use it:

```c
uint32_t
astra_assign_resolve(const AstraAssignTable *table, const char *path,
                     uint32_t rights, uint32_t member, char *wire,
                     uint32_t capacity, const AstraAssign **assign)
```

and replace

```c
    found = astra_assign_lookup(table, name);
```

with

```c
    found = astra_assign_member(table, name, member);
```

- [ ] **Step 5: Update the five production call sites**

Each takes `0u` as the new fourth argument, and each keeps a note of why zero
is right there. `sw/userspace/supervisor/src/events_host.c:93` and
`sw/userspace/commands/events/events.c:432` resolve `EVENTS:` and `SYS:`, which
are single-member names:

```c
    catalog_status = astra_assign_resolve(supervisor_assigns(),
                                          "SYS:" ASTRA_EVENT_CATALOG_NAME,
                                          ASTRA_RIGHT_READ, 0u, catalog_path,
                                          sizeof(catalog_path), &assign);
```

`console_shell.c:123`, `:292`, `:546` and `:569` take `0u` for now; tasks 6 and
7 replace the last two with a loop.

- [ ] **Step 6: Run the tests**

```sh
cd sw/userspace && make test && make sanitize && make analyze
```

Expected: `ASTRA VFS ASSIGN PASS` among the PASS lines; no sanitizer or
analyzer diagnostics.

- [ ] **Step 7: Commit**

```bash
git add sw/userspace/vfs sw/userspace/supervisor/src sw/userspace/commands
git commit -m "feat(assign): a member is a repeated name, and resolution says which"
```

---

### Task 4: Seeding binds the first and joins the rest

**Files:**
- Modify: `sw/userspace/vfs/src/vfs_assign.c` (`astra_assign_seed`)
- Modify: `sw/userspace/vfs/include/astra/vfs_assign.h` (its comment)
- Test: `sw/userspace/vfs/tests/test_vfs_assign.c`

**Interfaces:**
- Consumes: `astra_assign_join`, `astra_assign_member` (task 3);
  `AstraStartupCapability.root` (task 1).
- Produces: `astra_assign_seed` binding at the granted root and joining
  repeats.

- [ ] **Step 1: Write the failing test**

Add to `sw/userspace/vfs/tests/test_vfs_assign.c`. The file already has a
`capability(...)` helper used by the seeding tests; extend it or add a second
one that sets a root, whichever the existing signature makes cleaner:

```c
static void
capability_rooted(AstraStartupCapability *entry, const char *name,
                  uint32_t handle, uint32_t flags, const char *root)
{
    memset(entry, 0, sizeof(*entry));
    astra_capability_name_set(entry->name, name);
    entry->handle = handle;
    entry->rights = ASTRA_RIGHT_SIGNAL;
    entry->flags = ASTRA_CAPABILITY_FLAG_NAMESPACE | flags;
    astra_capability_root_set(entry->root, root);
}

static void
test_seeding_builds_a_union(void)
{
    AstraAssignTable table;
    AstraStartupCapability capabilities[3];
    char wire[ASTRA_VFS_PATH_MAX];

    capability_rooted(&capabilities[0], "WORK", 4u,
                      ASTRA_CAPABILITY_FLAG_READ | ASTRA_CAPABILITY_FLAG_WRITE,
                      "work");
    capability_rooted(&capabilities[1], "COMMANDS", 5u,
                      ASTRA_CAPABILITY_FLAG_READ | ASTRA_CAPABILITY_FLAG_WRITE,
                      "local/commands");
    capability_rooted(&capabilities[2], "COMMANDS", 5u,
                      ASTRA_CAPABILITY_FLAG_READ, "commands");

    assert(astra_assign_seed(&table, capabilities, 3u) == ASTRA_VFS_OK);
    assert(table.count == 3u);

    /*
     * The root travels now. Before this a child's COMMANDS: was bound at its
     * mount's own root, so a bare name resolved against the whole volume.
     */
    assert(astra_assign_resolve(&table, "WORK:notes", ASTRA_RIGHT_READ, 0u,
                                wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/work/notes") == 0);

    /* Order in the capability table is order in the namespace. */
    assert(astra_assign_resolve(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                0u, wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/local/commands/status") == 0);
    assert(astra_assign_resolve(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                1u, wire, sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/commands/status") == 0);
    assert(astra_assign_member(&table, "COMMANDS", 1u)->rights ==
           ASTRA_RIGHT_READ);
}
```

Call it from `main` after `test_seeding_from_a_capability_table();`.

- [ ] **Step 2: Run it to verify it fails**

```sh
cd sw/userspace/vfs && make test 2>&1 | tail -20
```

Expected: `Assertion 'strcmp(wire, "/work/notes") == 0' failed` — seeding still
binds at the mount root.

- [ ] **Step 3: Implement**

In `astra_assign_seed`, replace the `astra_assign_bind(...) == ...` block with:

```c
        char root[ASTRA_ASSIGN_ROOT_MAX];
        uint32_t status;

        copy(name, capabilities[index].name, ASTRA_CAPABILITY_NAME_MAX);
        copy(root, capabilities[index].root, ASTRA_ASSIGN_ROOT_MAX);
        /*
         * The first record of a name binds and every later one joins, so order
         * in the capability table is order in the namespace. The grant array is
         * then the authority manifest for the child's search order as well as
         * for its authority: one list, read once, meaning one thing.
         */
        if (astra_assign_lookup(table, name) == NULL) {
            status = astra_assign_bind(table, name, capabilities[index].handle,
                                       rights, root);
        } else {
            status = astra_assign_join(table, name, capabilities[index].handle,
                                       rights, root);
        }
        if (status == ASTRA_VFS_ERR_LIMIT) {
            return ASTRA_VFS_ERR_LIMIT;
        }
```

and delete the now-duplicated `copy(name, ...)` above it. Replace the header
comment paragraph beginning "No root travels in the published capability table
yet" with:

```
 * The root travels in the record and is bound with the name, so a child's
 * COMMANDS: means the directory it was granted rather than the whole volume.
 * A name granted twice is a union: the first record binds and each later one
 * joins, in the order the launcher listed them.
```

- [ ] **Step 4: Run the tests**

```sh
cd sw/userspace && make test && make sanitize && make analyze
```

Expected: `ASTRA VFS ASSIGN PASS`, clean sanitize and analyze.

- [ ] **Step 5: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(assign): a seeded namespace keeps its roots and its order"
```

---

### Task 5: The Kit's loop

The one place that turns member indices into an open file. A new translation
unit, because `vfs_client.c` is linked into a host test that does not carry
`vfs_assign.c` and adding assign calls there would break that link.

**Files:**
- Create: `sw/userspace/vfs/include/astra/vfs_union.h`
- Create: `sw/userspace/vfs/src/vfs_union.c`
- Create: `sw/userspace/vfs/tests/test_vfs_union.c`
- Modify: `sw/userspace/vfs/Makefile` (`CORE_SOURCES`, `HEADERS`,
  `TARGET_OBJECTS`, a new host-test target, and the `test` target's
  dependencies)

**Interfaces:**
- Consumes: `astra_assign_resolve` with a member index (task 3),
  `astra_vfs_open` from `astra/vfs_client.h`.
- Produces:
  - `typedef AstraVfsClient *(*AstraVfsAssignClientFn)(const AstraAssign *assign, void *context);`
  - `uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path, uint32_t rights, uint32_t flags, AstraVfsAssignClientFn client_for, void *context, char *wire, uint32_t capacity, AstraVfsFile *file, uint64_t *size, uint16_t *kind, AstraVfsClient **client, uint32_t *member);`

- [ ] **Step 1: Write the failing test**

Create `sw/userspace/vfs/tests/test_vfs_union.c`:

```c
/*
 * The Kit's loop over a union's members, against a client that records what it
 * was asked for and answers for exactly one path. Resolution is pure and the
 * trying happens here, so this is where "the first that opens wins" is proved.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/vfs_union.h>

static char asked[8][ASTRA_VFS_PATH_MAX];
static uint32_t asked_count;
static const char *answers;

/*
 * The seam is astra_vfs_open: this file replaces it, so what is under test is
 * the loop and nothing beneath it.
 */
uint32_t
astra_vfs_open(AstraVfsClient *client, const char *path, uint32_t flags,
               AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    (void)client;
    (void)flags;
    if (asked_count < 8u) {
        strncpy(asked[asked_count], path, ASTRA_VFS_PATH_MAX - 1u);
        asked[asked_count][ASTRA_VFS_PATH_MAX - 1u] = '\0';
        ++asked_count;
    }
    if (answers == NULL || strcmp(path, answers) != 0) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (file != NULL) {
        *file = 42u;
    }
    if (size != NULL) {
        *size = 0u;
    }
    if (kind != NULL) {
        *kind = ASTRA_VFS_KIND_FILE;
    }
    return ASTRA_VFS_OK;
}

static AstraVfsClient standin;

static AstraVfsClient *
client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    return assign->handle != 0u ? &standin : NULL;
}

static void
union_table(AstraAssignTable *table)
{
    astra_assign_table_init(table);
    assert(astra_assign_bind(table, "COMMANDS", 9u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "local/commands") == ASTRA_VFS_OK);
    assert(astra_assign_join(table, "COMMANDS", 9u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_OK);
}

static void
test_the_first_member_that_opens_wins(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    asked_count = 0u;
    answers = "/local/commands/status";
    assert(astra_vfs_assign_open(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_OK);
    assert(member == 0u);
    assert(file == 42u);
    assert(client == &standin);
    assert(strcmp(wire, "/local/commands/status") == 0);
    /* It stopped at the first that answered rather than trying both. */
    assert(asked_count == 1u);
}

static void
test_a_member_that_does_not_have_it_is_skipped(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    asked_count = 0u;
    answers = "/commands/status";
    assert(astra_vfs_assign_open(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_OK);
    assert(member == 1u);
    assert(asked_count == 2u);
    assert(strcmp(asked[0], "/local/commands/status") == 0);
    assert(strcmp(asked[1], "/commands/status") == 0);
}

static void
test_no_member_has_it(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    asked_count = 0u;
    answers = NULL;
    assert(astra_vfs_assign_open(&table, "COMMANDS:nothing", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(asked_count == 2u);

    /* A name nobody bound is refused without asking any disk anything. */
    asked_count = 0u;
    assert(astra_vfs_assign_open(&table, "APPS:status", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(asked_count == 0u);
}

static void
test_a_member_without_the_rights_is_skipped(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    asked_count = 0u;
    answers = "/local/commands/new";
    /*
     * Creation goes to the primary, which is the first member holding write
     * rights -- not a stored field, but a consequence of a fixed order.
     */
    assert(astra_vfs_assign_open(&table, "COMMANDS:new", ASTRA_RIGHT_WRITE,
                                 ASTRA_VFS_OPEN_WRITE, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_OK);
    assert(member == 0u);
    assert(asked_count == 1u);
}

int
main(void)
{
    test_the_first_member_that_opens_wins();
    test_a_member_that_does_not_have_it_is_skipped();
    test_no_member_has_it();
    test_a_member_without_the_rights_is_skipped();
    puts("ASTRA VFS UNION PASS");
    return 0;
}
```

`ASTRA_VFS_OPEN_READ` and `ASTRA_VFS_OPEN_WRITE` are defined in
`sw/include/astra/vfs_service.h:69-70` and reach this file through
`astra/vfs_client.h`.

- [ ] **Step 2: Run it to verify it fails**

```sh
cd sw/userspace/vfs && make build/host/test_vfs_union 2>&1 | tail -5
```

Expected: `No rule to make target 'build/host/test_vfs_union'`.

- [ ] **Step 3: Write the header**

Create `sw/userspace/vfs/include/astra/vfs_union.h`:

```c
#ifndef ASTRA_VFS_UNION_H
#define ASTRA_VFS_UNION_H

#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>

/*
 * Opening a path through a name that may have more than one member.
 *
 * Resolution is a string operation and stays one: it answers per member and
 * refuses once the index passes the last. The trying belongs here, where the
 * I/O already is, and this is the only place that does it -- the shell and a
 * launched program call the same function, which is what makes a union cross a
 * process boundary rather than being a shell feature.
 */

/*
 * Which client speaks for a member. A callback for the same reason the
 * transport is one: the supervisor maps an assign onto one of several clients
 * it owns, a launched program has one per handle it was granted, and neither
 * should be a special case inside the Kit.
 */
typedef AstraVfsClient *(*AstraVfsAssignClientFn)(const AstraAssign *assign,
                                                  void *context);

/*
 * Tries each member in order and stops at the first that opens. `wire` receives
 * the path that answered and `*member` its index -- which is the answer to
 * "which one ran", held by the loop that found it rather than deduced
 * afterwards.
 *
 * `rights` is what the operation needs and is checked per member, so a
 * read-only member under a writable union is skipped for a write rather than
 * refusing the whole call. Returns what the last attempt returned when nothing
 * answered, and ASTRA_VFS_ERR_NOT_FOUND when the name has no members at all.
 */
uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                               uint32_t rights, uint32_t flags,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *wire, uint32_t capacity,
                               AstraVfsFile *file, uint64_t *size,
                               uint16_t *kind, AstraVfsClient **client,
                               uint32_t *member);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `sw/userspace/vfs/src/vfs_union.c`:

```c
/*
 * The loop over a union's members. Everything it knows about a union is that
 * resolution stops answering at some index; everything it knows about I/O is
 * that a client opens a path.
 */

#include <astra/vfs_union.h>

#include <stddef.h>

uint32_t
astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                      uint32_t rights, uint32_t flags,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsFile *file,
                      uint64_t *size, uint16_t *kind, AstraVfsClient **client,
                      uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (table == NULL || client_for == NULL || wire == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved;

        resolved = astra_assign_resolve(table, path, rights, index, wire,
                                        capacity, &assign);
        /*
         * Past the last member, and the loop is over. A member that refused
         * for its own reasons -- rights it does not carry, a `..` that would
         * climb out of it -- is a member that did not answer, and the next one
         * still might.
         */
        if (resolved == ASTRA_VFS_ERR_NOT_FOUND && index != 0u) {
            break;
        }
        if (resolved != ASTRA_VFS_OK) {
            status = resolved;
            if (resolved == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL) {
            status = ASTRA_VFS_ERR_NOT_FOUND;
            continue;
        }
        status = astra_vfs_open(serving, wire, flags, file, size, kind);
        if (status != ASTRA_VFS_OK) {
            continue;
        }
        if (client != NULL) {
            *client = serving;
        }
        if (member != NULL) {
            *member = index;
        }
        return ASTRA_VFS_OK;
    }
    return status;
}
```

- [ ] **Step 5: Add it to the build**

In `sw/userspace/vfs/Makefile`:

```make
CORE_SOURCES := src/vfs_service_core.c src/vfs_client.c \
	src/vfs_local_transport.c src/vfs_port_transport.c src/vfs_assign.c \
	src/vfs_path.c src/vfs_union.c
```

add `include/astra/vfs_union.h` to `HEADERS`, add `build/m68k/vfs_union.o` to
`TARGET_OBJECTS`, and add the host test beside `ASSIGN_TEST`:

```make
UNION_TEST := build/host/test_vfs_union

$(UNION_TEST): src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_union.c $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(COMMON_FLAGS) $(HOST_FLAGS) \
		src/vfs_union.c src/vfs_assign.c src/vfs_path.c \
		tests/test_vfs_union.c -o $@
```

then add `$(UNION_TEST)` to whatever the `test` target builds and runs, beside
`$(ASSIGN_TEST)`, and to the `sanitize` and `analyze` targets in the same shape
the assign test uses. Read the bottom of the Makefile first —
`sed -n '90,144p' sw/userspace/vfs/Makefile` — and copy the assign test's own
lines rather than guessing at them.

- [ ] **Step 6: Run the tests**

```sh
cd sw/userspace/vfs && make test 2>&1 | tail -20
cd sw/userspace && make test && make sanitize && make analyze && make all
```

Expected: `ASTRA VFS UNION PASS` among the PASS lines, everything else still
passing, clean sanitize and analyze.

- [ ] **Step 7: Commit**

```bash
git add sw/userspace/vfs
git commit -m "feat(vfs): the Kit tries a union's members, because the Kit has the disk"
```

---

### Task 6: The supervisor's two members

**Files:**
- Modify: `sw/userspace/supervisor/src/vfs_host.c:59-113` (`bind_standard_assigns`)
- Modify: `sw/userspace/supervisor/src/console_shell.c:599-...` (`launch_grants`)

**Interfaces:**
- Consumes: `astra_assign_join` (task 3), `AstraLaunchGrant.root` (task 1).
- Produces: a `COMMANDS:` with two members in the supervisor's table, and a
  launch that grants both.

- [ ] **Step 1: Bind the union**

In `bind_standard_assigns`, replace the `COMMANDS` block with:

```c
    /*
     * Where programs live, and a union: the person's own directory first, then
     * the shipped one. A name found in `local/commands` shadows the command
     * the system shipped, which is what override means -- and the shadowing is
     * visible, because a listing shows both and a launch records which member
     * answered.
     *
     * Both members are made if they are missing and omitted rather than fatal
     * if the volume refuses, the same shape WORK: has. A volume with no
     * commands directory has not had one installed yet.
     */
    status = astra_vfs_mkdir(&vfs_client, "/local");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        status = astra_vfs_mkdir(&vfs_client, "/local/commands");
    }
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "COMMANDS", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                "local/commands");
    } else {
        /*
         * Said once, and the union keeps working. A name that silently returns
         * less than it did yesterday is worse than a name that says why.
         */
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "COMMANDS: local member skipped, mkdir refused with "
                     "status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/commands");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        /*
         * Bind if the first member never made it, join if it did: the shipped
         * member answers on its own rather than the name vanishing with the
         * writable one.
         */
        if (astra_assign_lookup(&vfs_assigns, "COMMANDS") == NULL) {
            (void)astra_assign_bind(&vfs_assigns, "COMMANDS", vfs_handle,
                                    ASTRA_RIGHT_READ, "commands");
        } else {
            (void)astra_assign_join(&vfs_assigns, "COMMANDS", vfs_handle,
                                    ASTRA_RIGHT_READ, "commands");
        }
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "COMMANDS: shipped member skipped, mkdir refused with "
                     "status %u", status);
    }
```

- [ ] **Step 2: Grant both members**

In `launch_grants` in `console_shell.c`, the mount loop currently walks three
names and binds each once. Replace it so it walks the shell's own table by
member, which makes the grant array a copy of the namespace rather than a
second statement of it:

```c
    /*
     * The namespace, member by member. A name with two members is granted
     * twice, in order, because a member is a repeated name on both sides of a
     * launch -- and a child that was handed only the first member would be a
     * child whose COMMANDS: quietly means less than the prompt's.
     *
     * The root travels now, so a child's COMMANDS: means the directory it was
     * granted rather than the whole volume.
     */
    for (uint32_t index = 0u; index < 3u && count < ASTRA_LAUNCH_GRANT_MAX;
         ++index) {
        for (uint32_t member = 0u; count < ASTRA_LAUNCH_GRANT_MAX; ++member) {
            const AstraAssign *assign =
                astra_assign_member(supervisor_assigns(), mount_names[index],
                                    member);

            if (assign == NULL) {
                break;
            }
            astra_capability_name_set(grants[count].name, mount_names[index]);
            grants[count].handle = assign->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((assign->rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((assign->rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            astra_capability_root_set(grants[count].root, assign->root);
            ++count;
        }
    }
```

Read the existing block first — it computes `mounts[]` and `mount_flags[]`
from the same table — and delete whatever it replaces rather than leaving both.
Keep the comment above the function about `SYS:` being the one left out; it is
still true and still deliberate.

- [ ] **Step 3: Build and run the terminal gate**

```sh
cd sw/userspace && make all
cd sw/userspace/commands && make
cd sw/boot && make astra_boot.bin
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
```

Expected: 22 of 22 still pass. `status 7` and `commands:status 3` now resolve
through member 1, the shipped `commands` directory, because that is where the
gate installs them.

- [ ] **Step 4: Commit**

```bash
git add sw/userspace/supervisor/src
git commit -m "feat(supervisor): COMMANDS: is two members, the person's first"
```

---

### Task 7: The shell — lookup, listing, and `assign`

**Files:**
- Modify: `sw/userspace/supervisor/src/console_shell.c` (`launch_path`,
  `command_ls`, a new `command_assign`, the builtin table and the help line)

**Interfaces:**
- Consumes: `astra_assign_member` (task 3), `astra_vfs_assign_open` (task 5).
- Produces: nothing later tasks call; the gate reads its output.

- [ ] **Step 1: Loop the members in `launch_path`**

Replace the two-place loop in `launch_path` (`console_shell.c:556-579`) with
one that walks members inside each place, and report which member answered:

```c
    for (uint32_t place = 0u; place < 2u; ++place) {
        uint32_t length = 0u;

        while (places[place][length] != '\0' && length + 1u < sizeof(typed)) {
            typed[length] = places[place][length];
            ++length;
        }
        for (uint32_t index = 0u;
             word[index] != '\0' && length + 1u < sizeof(typed); ++index) {
            typed[length++] = word[index];
        }
        typed[length] = '\0';
        /*
         * Every member of the place, in order, and the first that opens wins.
         * The index is the answer to "which one ran": a path variable cannot
         * answer that question, and a union is holding it by the time the
         * child starts.
         */
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *assign = NULL;

            status = astra_assign_resolve(supervisor_assigns(), typed,
                                          ASTRA_RIGHT_READ, member, wire,
                                          capacity, &assign);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            if (status != ASTRA_VFS_OK) {
                continue;
            }
            *client = supervisor_vfs_client_for(assign);
            if (*client == NULL) {
                continue;
            }
            {
                uint16_t kind = 0u;

                if (astra_vfs_stat(*client, wire, NULL, &kind) !=
                    ASTRA_VFS_OK) {
                    continue;   /* not on this member: try the next */
                }
                if (kind == ASTRA_VFS_KIND_DIRECTORY) {
                    continue;
                }
            }
            ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL,
                         ASTRA_EVENT_LEVEL_NOTICE,
                         "launching from place %u member %u", place, member);
            return ASTRA_VFS_OK;
        }
    }
    return status;
```

`ASTRA_VFS_KIND_DIRECTORY` and `astra_vfs_stat` are already used in
`command_cd` in this file. Use `astra_vfs_stat` rather than
`astra_vfs_assign_open` here because the launch path reads the image with the
shell's own client afterwards and an open file handle would have to be threaded
through it; the Kit helper is for callers that want the file.

- [ ] **Step 2: List every member**

Replace `command_ls`'s single listing with one pass per member. Only the
top-level form of a name unions — `ls COMMANDS:` — and a path inside it
resolves through whichever member holds it, which the loop already does:

```c
static void command_ls(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    char path[SHELL_PATH_MAX];
    char name[ASTRA_VFS_NAME_MAX];
    const AstraAssign *assign = NULL;
    AstraVfsClient *client = NULL;
    uint32_t shown = 0u;
    uint32_t members = 0u;
    uint32_t status;

    if (storage() == NULL) {
        write_line("ls: no volume");
        return;
    }
    status = astra_path_qualify(shell.assign, shell.directory,
                                argc > 1 ? argv[1] : NULL, typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    /*
     * Every member, in order, and nothing is deduplicated: a name on two
     * members is shown twice with the member it came from. A duplicate-name
     * set would be memory proportional to the directory, which every
     * enumeration on this machine refuses -- and hiding the loser would make a
     * listing disagree with what a lookup would do.
     */
    for (uint32_t member = 0u; ; ++member) {
        uint64_t cursor = 0u;
        uint16_t kind = 0u;

        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, member, path,
                                      sizeof(path), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            break;
        }
        if (status != ASTRA_VFS_OK) {
            continue;
        }
        client = supervisor_vfs_client_for(assign);
        if (client == NULL) {
            continue;
        }
        ++members;
        for (;;) {
            status = astra_vfs_readdir(client, path, cursor, name,
                                       sizeof(name), &kind, &cursor);
            if (status == ASTRA_VFS_ERR_NOT_FOUND)
                break;
            if (status != ASTRA_VFS_OK) {
                report_status("ls", status);
                return;
            }
            astra_terminal_write(&shell.terminal, name);
            if (kind == ASTRA_VFS_KIND_DIRECTORY)
                astra_terminal_putc(&shell.terminal, '/');
            /*
             * The member it came from, so the shadowing a person cannot see in
             * a name is visible in the listing.
             */
            astra_terminal_write(&shell.terminal, "  [");
            write_number(member);
            astra_terminal_write(&shell.terminal, "]");
            astra_terminal_putc(&shell.terminal, '\n');
            ++shown;
        }
    }
    if (members == 0u) {
        report_status("ls", ASTRA_VFS_ERR_NOT_FOUND);
        return;
    }
    if (shown == 0u)
        write_line("(empty)");
}
```

- [ ] **Step 3: Add the `assign` builtin**

Add beside the other `command_*` functions:

```c
/*
 * What this shell's namespace actually is: every name, its members in order,
 * and what each member carries.
 *
 * It is a builtin rather than a program because it prints the *shell's*
 * namespace. A launched program holds its own, so a program answering this
 * question would truthfully answer about itself and be read as answering about
 * the prompt.
 *
 * It is read-only. Joining a member at the prompt is a shell language decision
 * the layout spec defers, and nothing needs to rebind at runtime yet.
 */
static void command_assign(int argc, char *const *argv)
{
    const AstraAssignTable *table = supervisor_assigns();

    (void)argc;
    (void)argv;
    for (uint32_t index = 0u; index < table->count; ++index) {
        const AstraAssign *entry = &table->entries[index];
        uint32_t member = 0u;

        /* Its position among the members of its own name. */
        for (uint32_t at = 0u; at < index; ++at) {
            if (astra_capability_name_equal(table->entries[at].name,
                                            entry->name)) {
                ++member;
            }
        }
        astra_terminal_write(&shell.terminal, entry->name);
        astra_terminal_write(&shell.terminal, ": [");
        write_number(member);
        astra_terminal_write(&shell.terminal, "] ");
        astra_terminal_write(&shell.terminal,
                             (entry->rights & ASTRA_RIGHT_WRITE) != 0u ?
                                 "rw " : "r  ");
        astra_terminal_putc(&shell.terminal, '/');
        astra_terminal_write(&shell.terminal, entry->root);
        astra_terminal_putc(&shell.terminal, '\n');
    }
}
```

The builtins are an if/else chain rather than a table. In `run_line`, at
`console_shell.c:899-901`, add a branch before the `else` that launches:

```c
    else if (shell_equal(words.argv[0], "rm"))
        command_rm(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "assign"))
        command_assign(words.argc, words.argv);
    else
```

and extend the help text. `console_shell.c:501` lists the builtins and `:510`
says where programs live:

```c
    write_line("builtins: ls [dir], cd [dir], cat FILE, mkdir DIR,");
```

becomes

```c
    write_line("builtins: ls [dir], cd [dir], cat FILE, mkdir DIR, assign,");
```

and after the `programs live in COMMANDS:` line add:

```c
    write_line("assign shows every name and its members, in the order tried");
```

- [ ] **Step 4: Build and run the gate**

```sh
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/userspace/commands && make
cd sw/boot && make astra_boot.bin
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
```

Expected: every line still passes. `ls` output now carries a `[0]` or `[1]`
column, which the existing expectations do not match on — they check for
substrings like `proto/`, and `proto/  [0]` still contains it.

- [ ] **Step 5: Commit**

```bash
git add sw/userspace/supervisor/src/console_shell.c
git commit -m "feat(shell): a union is looked in, listed, and printed back"
```

---

### Task 8: `which`, and the gate that proves a child resolves a union

**Files:**
- Create: `sw/userspace/commands/which/which.c`
- Modify: `sw/userspace/commands/Makefile:34` (`COMMANDS`)
- Modify: `emu/qemu/astra_image.py` (install a shadowing command into the
  writable member)
- Modify: `emu/qemu/test-terminal.py` (`SCRIPT`)

**Interfaces:**
- Consumes: `astra_assign_seed` with roots and members (task 4),
  `astra_vfs_assign_open` (task 5), the two-member grant (task 6).
- Produces: a `which` command on the volume.

- [ ] **Step 1: Write `which`**

Create `sw/userspace/commands/which/which.c`:

```c
/*
 * `which` -- where a bare name would be found, and on which member.
 *
 * The question a search path cannot answer. A union is a binding rather than a
 * string: its members were joined by the startup sequence, they carry their own
 * rights, and no program can extend the list -- so "which one would run" has an
 * answer, and this prints it.
 *
 * It is also the proof that a union crosses a process boundary. This program
 * holds COMMANDS: as two grants with two roots, seeds them into its own
 * namespace, and loops them with the same Kit function the shell uses.
 */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("which", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define WHICH_PATH_MAX 128u
#define WHICH_CLIENT_MAX 4u

/* Statically allocated, because a user thread gets one 4 KiB stack. */
static AstraAssignTable assigns;
static uint32_t out;

/*
 * One client per distinct handle, made when a member first needs it. Two
 * members of one union on one volume share a handle and therefore a client;
 * two on different volumes would not, and nothing here has to change for that.
 */
static struct {
    uint32_t handle;
    AstraVfsClient client;
    int connected;
} clients[WHICH_CLIENT_MAX];

static void
say(const char *text)
{
    (void)astra_print(out, text);
}

static void
say_number(uint32_t value)
{
    char digits[12];
    char text[13];
    uint32_t index = 0u;
    uint32_t at = 0u;

    if (value == 0u) {
        digits[index++] = '0';
    }
    while (value != 0u && index < sizeof(digits)) {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (index != 0u) {
        text[at++] = digits[--index];
    }
    text[at] = '\0';
    (void)astra_print(out, text);
}

static AstraVfsClient *
client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    for (uint32_t index = 0u; index < WHICH_CLIENT_MAX; ++index) {
        if (clients[index].connected &&
            clients[index].handle == assign->handle) {
            return &clients[index].client;
        }
    }
    for (uint32_t index = 0u; index < WHICH_CLIENT_MAX; ++index) {
        if (clients[index].connected) {
            continue;
        }
        clients[index].handle = assign->handle;
        if (astra_vfs_connect(&clients[index].client,
                              astra_vfs_port_transport,
                              &clients[index].handle) != ASTRA_VFS_OK) {
            return NULL;
        }
        clients[index].connected = 1;
        return &clients[index].client;
    }
    return NULL;
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const uint32_t *argv = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char typed[WHICH_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 0u;
    uint32_t at = 0u;
    uint32_t status;

    if (startup == NULL || startup->capabilities_address == 0u) {
        return ASTRA_STATUS_INVALID;
    }
    capabilities =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;
    if (startup->argc != 0u && startup->argv_address != 0u) {
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    }
    if (astra_assign_seed(&assigns, capabilities,
                          startup->capability_count) != ASTRA_VFS_OK) {
        return ASTRA_STATUS_INVALID;
    }
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT")) {
            out = capabilities[index].handle;
        }
    }
    if (out == 0u) {
        return ASTRA_STATUS_ACCESS;
    }
    if (startup->argc < 2u || argv == NULL) {
        say("which: name it\n");
        return ASTRA_STATUS_INVALID;
    }

    at = 0u;
    {
        static const char prefix[] = "COMMANDS:";
        const char *word = (const char *)(uintptr_t)argv[1];

        while (prefix[at] != '\0' && at + 1u < sizeof(typed)) {
            typed[at] = prefix[at];
            ++at;
        }
        for (uint32_t index = 0u;
             word[index] != '\0' && at + 1u < sizeof(typed); ++index) {
            typed[at++] = word[index];
        }
        typed[at] = '\0';
    }

    status = astra_vfs_assign_open(&assigns, typed, ASTRA_RIGHT_READ,
                                   ASTRA_VFS_OPEN_READ, client_for, NULL,
                                   wire, sizeof(wire), &file, NULL, NULL,
                                   &client, &member);
    if (status != ASTRA_VFS_OK) {
        say("which: not on any member, status ");
        say_number(status);
        say("\n");
        return (int)status;
    }
    (void)astra_vfs_close(client, file);
    /* The path that answered, and the member index that is the answer. */
    say(wire);
    say(" [");
    say_number(member);
    say("]\n");
    return ASTRA_STATUS_OK;
}
```

`ASTRA_STATUS_OK` is `sw/include/astra/status.h:19` and `ASTRA_STATUS_INVALID`
is line 38; `ASTRA_VFS_OPEN_READ` is `sw/include/astra/vfs_service.h:69`.

- [ ] **Step 2: Add it to the command list**

In `sw/userspace/commands/Makefile`:

```make
COMMANDS := status events devices which
```

- [ ] **Step 3: Build it and confirm it is on the volume**

```sh
cd sw/userspace/commands && make
ls -l build/m68k/which
strings build/m68k/which | grep "not on any member"
```

Expected: the image exists and the string is in it. This is the check that
catches a stale binary, which has cost a full run and a wrong conclusion before.

- [ ] **Step 4: Install a shadowing command into the writable member**

In `emu/qemu/astra_image.py`, add beside `COMMANDS_DIRECTORY`:

```python
COMMANDS_DIRECTORY = "commands"
# The writable member of COMMANDS:, and the reason it exists in this gate: a
# command installed here shadows the one the system shipped, which is what a
# union is for and what nothing else on the volume can demonstrate.
LOCAL_COMMANDS_DIRECTORY = "local/commands"
```

and, inside `install`, after the shipped commands are written:

```python
        # The shadowing pair. `which` is installed on both members under two
        # names: the shipped one stays where it is, and a copy goes into the
        # writable member under the same name as a shipped command, so a
        # lookup has a real choice to make and the gate can see which it made.
        _debugfs(volume, "mkdir /local", "the local directory", optional=True)
        _debugfs(volume, "mkdir /%s" % LOCAL_COMMANDS_DIRECTORY,
                 "the local commands directory", optional=True)
        for name, path in built:
            if name != "which":
                continue
            target = "/%s/%s" % (LOCAL_COMMANDS_DIRECTORY, "devices")
            _debugfs(volume, "rm %s" % target, "the old local command",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target[1:]),
                     "a shadowing command")
```

Read the loop that installs the shipped commands first and match how it forms
its `write` argument — `debugfs` takes the target without a leading separator
in that call, and copying its exact shape is what stops this silently writing
nothing.

- [ ] **Step 5: Add the gate assertions**

In `emu/qemu/test-terminal.py`, add to `SCRIPT` after the `devices` line:

```python
    # The namespace printed back, and the whole of the order a lookup uses.
    # Before this existed the search order was a comment in one function.
    ("assign", "local/commands"),
    # A child resolving through a union it was granted. `which` holds COMMANDS:
    # as two grants with two roots and loops them with the same Kit function
    # the shell uses -- so this line is the roots-in-grants fix and the union
    # crossing a process boundary at once. `status` is only on the shipped
    # member, so member 1 is the honest answer.
    ("which status", "/commands/status [1]"),
    # And the shadowing. The gate installed a `devices` into the writable
    # member, so the person's own copy is what a lookup finds -- member 0,
    # ahead of the shipped one.
    ("which devices", "/local/commands/devices [0]"),
    # Which is also what runs: the shadowing copy is `which`'s image under
    # another name, so it answers the way `which` does rather than the way the
    # shipped `devices` does.
    ("devices status", "/commands/status [1]"),
    # Both members listed, nothing deduplicated, each against the member it
    # came from. A listing that hid the loser would disagree with the two
    # lines above it.
    ("ls commands:", "[1]"),
```

- [ ] **Step 6: Run the gate**

```sh
cd sw/userspace && make all
cd sw/userspace/commands && make
cd sw/boot && make astra_boot.bin
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
```

Expected: every line passes, including the five new ones, and no endpoint is
quarantined. If a line fails, the gate dumps the screen and the kernel ring —
read those before changing anything.

- [ ] **Step 7: Prove the missing member is skipped and said once**

Run by hand rather than added to the script, because it needs a volume the
supervisor cannot repair. **Deleting `/local/commands` does not test this** —
the supervisor makes the member if it is missing, so a deleted directory comes
back and nothing is ever skipped. The member has to be one `mkdir` refuses, so
`/local` is made a regular file:

```sh
cp /tmp/part-clean.img /tmp/part-blocked.img
printf 'not a directory' > /tmp/blocker
# The volume is lifted out, worked on, and put back -- debugfs cannot reopen an
# image?offset= target after a journal recovery. Same reason astra_image.py does.
dd if=/tmp/part-blocked.img of=/tmp/vol-blocked.img bs=512 skip=10240 count=120832
e2fsck -fy /tmp/vol-blocked.img
debugfs -w -R "rm /local/commands/devices" /tmp/vol-blocked.img
debugfs -w -R "rmdir /local/commands" /tmp/vol-blocked.img
debugfs -w -R "rmdir /local" /tmp/vol-blocked.img
debugfs -w -R "write /tmp/blocker local" /tmp/vol-blocked.img
dd if=/tmp/vol-blocked.img of=/tmp/part-blocked.img bs=512 seek=10240 conv=notrunc
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part-blocked.img --verbose
pkill -f qemu-system-m68k
```

The script will fail at the three lines that need the writable member, which is
correct and expected. What this run is for is the screen before that: type by
hand, or read the `--verbose` output, and confirm

- `which status` answers `/commands/status [1]` — the union keeps working from
  the survivor, which is the recoverable failure block spanning does not have;
- `assign` shows `COMMANDS:` with one member, `/commands`;
- `events --subsystem supervisor` shows the skip **exactly once**:
  `COMMANDS: local member skipped, mkdir refused with status 4`
  (`ASTRA_VFS_ERR_NOT_DIR`, `sw/include/astra/vfs_service.h:97`, which
  `report_status` prints as "not a directory"). One line, not one per lookup: a name that
  silently returns less than it did yesterday is worse than one that says why,
  and one that says why on every open is noise.

Record all three in the commit message.

- [ ] **Step 8: Run every gate**

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/kernel && make test \
    HOST_EXTRA_FLAGS="-DKERNEL_TRACE_BUILD_LEVEL=KERNEL_TRACE_LEVEL_DEBUG"
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/userspace/storage && make ext4-test
cd sw/boot && make astra_boot.bin
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-events.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
python3 emu/qemu/time-boot.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --runs 5 --budget 1.0
pkill -f qemu-system-m68k
```

and on the Mac:

```sh
python3 -m pytest tools/tests/
python3 -m pytest sw/boot/tests/
```

Expected: every one green. Record the supervisor text size and the boot time
in the commit message; the last known-good figures are 92,487 and 0.09s of a
1.00s budget.

- [ ] **Step 9: Commit**

```bash
git add sw/userspace/commands emu/qemu/astra_image.py emu/qemu/test-terminal.py
git commit -m "feat(which): a child resolves a union it was granted"
```

---

### Task 9: The record

**Files:**
- Modify: `docs/ABI.md` (the capability records and the grant ceiling)
- Modify: `docs/CURRENT_STATE.md` (the Active Arty migration override block)
- Modify: `docs/STATUS.md`
- Create: `docs/HANDOVER-union-assigns.md`
- Modify: `docs/superpowers/plans/2026-08-07-union-assigns.md` (this file — a
  "what the build settled" block under each finished task)

- [ ] **Step 1: Update the ABI record**

In `docs/ABI.md`, find the section describing `AstraStartupCapability` and
`AstraLaunchGrant` and update: both are 92 bytes, both carry
`root[64]`, `ASTRA_LAUNCH_GRANT_MAX` is 8, and the startup ABI version is
whatever task 1 bumped it to. State that the kernel carries the root and never
reads it, the same contract `flags` has.

- [ ] **Step 2: Write the handover**

Create `docs/HANDOVER-union-assigns.md` in the shape of
`docs/HANDOVER-launch.md`: where the code is, what the mechanisms are, the
traps that cost time during this milestone, how to run the gates, and what is
queued next. The next thing is the events store's durability —
`EVENTS:boot/-1` does not exist and the terminal gate asserts the refusal.

- [ ] **Step 3: Point CLAUDE.md at it**

In `CLAUDE.md`, change the **Current resume point** row of the "Where to read
next" table from `docs/HANDOVER-events.md` to `docs/HANDOVER-union-assigns.md`.

- [ ] **Step 4: Commit**

```bash
git add docs CLAUDE.md
git commit -m "docs: hand over at the union, and what a grant now carries"
```

---

## Notes for whoever executes this

- **Task 1 is the risky one.** It moves an ABI record's size, and seven kernel
  tests hardcoded a memory layout the last time a layout constant moved. If
  something fails in a place that looks unrelated to grants, check what it
  assumes about `ASTRA_STARTUP_CAPABILITY_SIZE` before assuming a real bug.
- **The startup page grew by about 640 bytes per process.** If `sw/boot`
  reports a ROM overflow or the K1 qualification build fails to link, that is
  where to look first, and `docs/MEMORY_MAP.md` is what has to be updated
  rather than the ceiling silently raised.
- **A stale command image is invisible.** `make all` in `sw/userspace` does not
  build `commands/`. Every time `which` behaves like a program you did not
  write, run `strings build/m68k/which | grep ...` before debugging the kernel.
- **Keep `/tmp/part-clean.img` clean.** The gate kills QEMU rather than
  shutting it down, so unclean mounts accumulate until `mkdir` starts answering
  `I/O error`. `docs/HANDOVER-launch.md` §5 has the repair.
