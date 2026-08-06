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
    /* A binding without a handle or without rights is not authority. */
    assert(astra_assign_bind(&table, "WORK", 0u, 1u) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, "WORK", 1u, 0u) ==
           ASTRA_VFS_ERR_INVALID);
    /* A space makes a name that cannot be typed back at the machine. */
    assert(astra_assign_bind(&table, "WO RK", 1u, 1u) ==
           ASTRA_VFS_ERR_INVALID);
    /* The colon is the separator, so it cannot be inside a name. */
    assert(astra_assign_bind(&table, "WORK:", 1u, 1u) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(NULL, "WORK", 1u, 1u) == ASTRA_VFS_ERR_INVALID);
    assert(astra_assign_bind(&table, NULL, 1u, 1u) == ASTRA_VFS_ERR_INVALID);

    /* Nothing above bound anything. */
    assert(astra_assign_lookup(&table, "WORK") == NULL);
    assert(astra_assign_lookup(&table, NULL) == NULL);
    assert(astra_assign_lookup(NULL, "WORK") == NULL);
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
    assert(astra_assign_lookup(&table, "WORK")->rights == 1u);
    assert(table.count == 1u);

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
    assert(astra_assign_bind(&table, "SYS", 1u, 1u) == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "WORK", 2u, 2u) == ASTRA_VFS_OK);
    assert(astra_assign_bind(&table, "TEMP", 3u, 3u) == ASTRA_VFS_OK);

    assert(astra_assign_unbind(&table, "SYS") == ASTRA_VFS_OK);
    assert(table.count == 2u);
    assert(astra_assign_lookup(&table, "SYS") == NULL);
    assert(astra_assign_lookup(&table, "WORK")->handle == 2u);
    assert(astra_assign_lookup(&table, "TEMP")->handle == 3u);
}

int
main(void)
{
    test_bind_and_look_up();
    test_refusals();
    test_rebinding_and_capacity();
    test_unbinding_keeps_the_rest_reachable();
    puts("ASTRA VFS ASSIGN PASS");
    return 0;
}
