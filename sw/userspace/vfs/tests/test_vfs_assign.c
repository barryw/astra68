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
    assert(astra_assign_bind(&table, "WORK", 7u, 3u, "work") == ASTRA_VFS_OK);

    found = astra_assign_lookup(&table, "WORK");
    assert(found != NULL);
    assert(found->handle == 7u);
    assert(found->rights == 3u);
    assert(strcmp(found->root, "work") == 0);

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

    assert(astra_assign_bind(&table, "", 1u, 1u, "") == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, oversized, 1u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);
    /* A binding without a handle or without rights is not authority. */
    assert(astra_assign_bind(&table, "WORK", 0u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "WORK", 1u, 0u, "") ==
           ASTRA_VFS_ERR_INVALID);
    /* A space makes a name that cannot be typed back at the machine. */
    assert(astra_assign_bind(&table, "WO RK", 1u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);
    /* The colon is the separator, so it cannot be inside a name. */
    assert(astra_assign_bind(&table, "WORK:", 1u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(NULL, "WORK", 1u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, NULL, 1u, 1u, "") ==
           ASTRA_VFS_ERR_INVALID);

    /* Nothing above bound anything. */
    assert(astra_assign_lookup(&table, "WORK") == NULL);
    assert(astra_assign_lookup(&table, NULL) == NULL);
    assert(astra_assign_lookup(NULL, "WORK") == NULL);
}

static void
test_roots_are_normalised(void)
{
    AstraAssignTable table;
    const AstraAssign *found;

    astra_assign_table_init(&table);
    /*
     * A root is stored as the volume-relative path it means, without the
     * leading separator: three spellings, one root.
     */
    assert(astra_assign_bind(&table, "A", 1u, 1u, "/work") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "B", 1u, 1u, "work/") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "C", 1u, 1u, "/./work") == ASTRA_VFS_OK);
    found = astra_assign_lookup(&table, "A");
    assert(strcmp(found->root, "work") == 0);
    assert(strcmp(astra_assign_lookup(&table, "B")->root, "work") == 0);
    assert(strcmp(astra_assign_lookup(&table, "C")->root, "work") == 0);

    /* The mount's own root is the empty root, not "/". */
    assert(astra_assign_bind(&table, "D", 1u, 1u, "") == ASTRA_VFS_OK);
    assert(strcmp(astra_assign_lookup(&table, "D")->root, "") == 0);

    /* `..` inside a root is arithmetic and lands at the mount's own root. */
    assert(astra_assign_bind(&table, "F", 1u, 1u, "/work/..") == ASTRA_VFS_OK);
    assert(strcmp(astra_assign_lookup(&table, "F")->root, "") == 0);

    /*
     * A root that climbs out of the mount is not a root. Binding is where
     * authority is handed over, so it is the last place this can be checked.
     */
    assert(astra_assign_bind(&table, "E", 1u, 1u, "..") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "E", 1u, 1u, "work/../..") ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "E", 1u, 1u, NULL) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_lookup(&table, "E") == NULL);
}

static void
test_rebinding_and_capacity(void)
{
    AstraAssignTable table;
    char name[8];
    uint32_t index;

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 7u, 3u, "old") == ASTRA_VFS_OK);
    /* Rebinding replaces, root and all: a name has one meaning at a time. */
    assert(astra_assign_bind(&table, "work", 9u, 1u, "new") == ASTRA_VFS_OK);
    assert(astra_assign_lookup(&table, "WORK")->handle == 9u);
    assert(astra_assign_lookup(&table, "WORK")->rights == 1u);
    assert(strcmp(astra_assign_lookup(&table, "WORK")->root, "new") == 0);
    assert(table.count == 1u);

    assert(astra_assign_unbind(&table, "WORK") == ASTRA_VFS_OK);
    assert(astra_assign_lookup(&table, "WORK") == NULL);
    assert(astra_assign_unbind(&table, "WORK") == ASTRA_VFS_ERR_NOT_FOUND);

    for (index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        snprintf(name, sizeof(name), "N%u", index);
        assert(astra_assign_bind(&table, name, index + 1u, 1u, "") ==
               ASTRA_VFS_OK);
    }
    assert(astra_assign_bind(&table, "ONEMORE", 1u, 1u, "") ==
           ASTRA_VFS_ERR_LIMIT);

    /* A full table still answers for what it holds. */
    assert(astra_assign_lookup(&table, "N0") != NULL);
    assert(astra_assign_lookup(&table, "N15") != NULL);
}

static void
test_unbinding_keeps_the_rest_reachable(void)
{
    AstraAssignTable table;

    /*
     * Unbinding moves the last entry into the hole, so the entry that moved
     * has to remain findable. A table that loses a name when a different one
     * is removed is worse than one that cannot remove at all.
     */
    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "SYS", 1u, 1u, "") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "WORK", 2u, 2u, "work") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "TEMP", 3u, 3u, "temp") == ASTRA_VFS_OK);

    assert(astra_assign_unbind(&table, "SYS") == ASTRA_VFS_OK);
    assert(table.count == 2u);
    assert(astra_assign_lookup(&table, "SYS") == NULL);
    assert(astra_assign_lookup(&table, "WORK")->handle == 2u);
    assert(strcmp(astra_assign_lookup(&table, "WORK")->root, "work") == 0);
    assert(astra_assign_lookup(&table, "TEMP")->handle == 3u);
    assert(strcmp(astra_assign_lookup(&table, "TEMP")->root, "temp") == 0);
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

    /* An assign rooted at the mount resolves to the mount. */
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

    /* A rest that normalises away names the assign's root. */
    assert(astra_assign_resolve(&table, "WORK:src/..", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/work") == 0);
    assert(astra_assign_resolve(&table, "SYS:.", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_OK);
    assert(strcmp(wire, "/") == 0);
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
    assert(astra_assign_resolve(&table, "WORK:src/../../etc",
                                ASTRA_RIGHT_READ, wire, sizeof(wire), NULL) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_assign_resolve(&table, "SYS:../etc", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);

    /* Not a path on this machine. */
    assert(astra_assign_resolve(&table, "/etc/passwd", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_resolve(&table, "main.c", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_resolve(&table, NULL, ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_INVALID);
    /* An empty namespace holds nothing, which is not the same as refusing. */
    assert(astra_assign_resolve(NULL, "WORK:x", ASTRA_RIGHT_READ, wire,
                                sizeof(wire), NULL) == ASTRA_VFS_ERR_NOT_FOUND);

    /* Truncation would name a different file. */
    assert(astra_assign_resolve(&table, "WORK:src/main.c", ASTRA_RIGHT_READ,
                                tiny, sizeof(tiny), NULL) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_resolve(&table, "WORK:x", ASTRA_RIGHT_READ, wire, 1u,
                                NULL) == ASTRA_VFS_ERR_INVALID);
}

/*
 * Seeding a namespace from what a launch handed over.
 *
 * A launched program's capability table *is* its namespace: there is no
 * manifest to read and no path to search, so the names it may use are exactly
 * the ones somebody granted it. This is the whole of that translation.
 */
static void
capability(AstraStartupCapability *entry, const char *name, uint32_t handle,
           uint32_t rights)
{
    astra_capability_name_set(entry->name, name);
    entry->handle = handle;
    /*
     * `rights` is the kernel's word and is not what a namespace entry is bound
     * with; the flags are. A published grant carries SIGNAL on a port handle
     * and says READ or WRITE about the mount, and those are different
     * vocabularies that cannot share a field.
     */
    entry->rights = ASTRA_RIGHT_SIGNAL;
    entry->flags = ASTRA_CAPABILITY_FLAG_NAMESPACE;
    if ((rights & ASTRA_RIGHT_READ) != 0u) {
        entry->flags |= ASTRA_CAPABILITY_FLAG_READ;
    }
    if ((rights & ASTRA_RIGHT_WRITE) != 0u) {
        entry->flags |= ASTRA_CAPABILITY_FLAG_WRITE;
    }
}

/* A capability that is authority without being a name: a stream, a device. */
static void
capability_unnamed(AstraStartupCapability *entry, const char *name,
                   uint32_t handle, uint32_t rights)
{
    capability(entry, name, handle, rights);
    entry->flags = 0u;
}

static void
test_seeding_from_a_capability_table(void)
{
    AstraStartupCapability capabilities[5];
    AstraAssignTable table;
    const AstraAssign *found;

    /* The shape the kernel publishes: its own two first, then the grants. */
    capability_unnamed(&capabilities[0], ASTRA_CAPABILITY_PROCESS, 1u,
                       ASTRA_RIGHT_READ);
    capability_unnamed(&capabilities[1], ASTRA_CAPABILITY_THREAD, 2u,
                       ASTRA_RIGHT_READ);
    capability(&capabilities[2], "WORK", 9u,
               ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE);
    capability(&capabilities[3], "EVENTS", 11u, ASTRA_RIGHT_READ);
    /*
     * A stream. It is a perfectly good capability with a perfectly good name
     * and it is not a namespace entry: `STDOUT:src/main.c` is nonsense. It is
     * excluded because it did not declare itself a name, which is the same
     * mechanism that excludes PROCESS and THREAD -- and the reason there is no
     * list here of capabilities that are not mounts.
     */
    capability_unnamed(&capabilities[4], "STDOUT", 13u, ASTRA_RIGHT_SIGNAL);

    assert(astra_assign_seed(&table, capabilities, 5u) == ASTRA_VFS_OK);

    assert(table.count == 2u);
    assert(astra_assign_lookup(&table, ASTRA_CAPABILITY_PROCESS) == NULL);
    assert(astra_assign_lookup(&table, ASTRA_CAPABILITY_THREAD) == NULL);
    assert(astra_assign_lookup(&table, "STDOUT") == NULL);

    found = astra_assign_lookup(&table, "WORK");
    assert(found != NULL);
    assert(found->handle == 9u);
    assert(found->rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));
    /* No root travels in the published table yet, so a grant is its mount. */
    assert(found->root[0] == '\0');

    found = astra_assign_lookup(&table, "EVENTS");
    assert(found != NULL);
    assert(found->handle == 11u);
    assert(found->rights == ASTRA_RIGHT_READ);

    /* A name is canonical however the grant spelled it. */
    capability(&capabilities[2], "work", 9u, ASTRA_RIGHT_READ);
    assert(astra_assign_seed(&table, capabilities, 3u) == ASTRA_VFS_OK);
    assert(astra_assign_lookup(&table, "WORK") != NULL);

    /* Seeding replaces a namespace rather than adding to one. */
    assert(astra_assign_seed(&table, capabilities, 2u) == ASTRA_VFS_OK);
    assert(table.count == 0u);
}

static void
test_seeding_refusals(void)
{
    AstraStartupCapability capabilities[3];
    AstraAssignTable table;

    capability(&capabilities[0], "WORK", 9u, ASTRA_RIGHT_READ);
    /*
     * A grant that is not a namespace name is skipped, not fatal. The
     * capability table is where every kind of authority a process holds is
     * published, so entries that are not mounts are the ordinary case rather
     * than a malformed table -- and one of them must not cost a child the names
     * it was given.
     */
    capability(&capabilities[1], "not a name", 10u, ASTRA_RIGHT_READ);
    /*
     * A grant that says it is a name and grants nothing on it would bind a
     * name that resolves and then refuses everything, which tells its holder
     * one call too late that it was given nothing.
     */
    capability(&capabilities[2], "EVENTS", 11u, 0u);

    assert(astra_assign_seed(&table, capabilities, 3u) == ASTRA_VFS_OK);
    assert(table.count == 1u);
    assert(astra_assign_lookup(&table, "WORK") != NULL);
    assert(astra_assign_lookup(&table, "EVENTS") == NULL);

    /* A table with nothing in it is a namespace with nothing in it. */
    assert(astra_assign_seed(&table, NULL, 0u) == ASTRA_VFS_OK);
    assert(table.count == 0u);

    assert(astra_assign_seed(NULL, capabilities, 1u) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_seed(&table, NULL, 1u) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_seed(&table, capabilities,
                             ASTRA_STARTUP_CAPABILITY_MAX + 1u) ==
           ASTRA_VFS_ERR_INVALID);
}

/*
 * More grants than a namespace holds. It cannot happen from one launch today --
 * six is the grant ceiling and sixteen the namespace's -- but the startup table
 * carries thirty-two, so the arithmetic that makes it impossible is somewhere
 * else. A namespace quietly missing the names past the sixteenth is the failure
 * this refuses to have.
 */
static void
test_seeding_beyond_capacity(void)
{
    AstraStartupCapability capabilities[ASTRA_ASSIGN_MAX + 2u];
    AstraAssignTable table;
    uint32_t index;

    for (index = 0u; index < ASTRA_ASSIGN_MAX + 2u; ++index) {
        char name[ASTRA_CAPABILITY_NAME_MAX];

        name[0] = 'N';
        name[1] = (char)('A' + (index / 10u));
        name[2] = (char)('0' + (index % 10u));
        name[3] = '\0';
        capability(&capabilities[index], name, index + 1u, ASTRA_RIGHT_READ);
    }
    assert(astra_assign_seed(&table, capabilities, ASTRA_ASSIGN_MAX + 2u) ==
           ASTRA_VFS_ERR_LIMIT);
    assert(table.count == ASTRA_ASSIGN_MAX);
}

int
main(void)
{
    test_bind_and_look_up();
    test_refusals();
    test_roots_are_normalised();
    test_rebinding_and_capacity();
    test_unbinding_keeps_the_rest_reachable();
    test_resolving();
    test_resolution_refusals();
    test_seeding_from_a_capability_table();
    test_seeding_refusals();
    test_seeding_beyond_capacity();
    puts("ASTRA VFS ASSIGN PASS");
    return 0;
}
