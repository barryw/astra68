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
