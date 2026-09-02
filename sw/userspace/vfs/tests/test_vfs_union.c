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
static char stat_asked[8][ASTRA_VFS_PATH_MAX];
static uint32_t stat_asked_count;
static const char *answers;
static const char *answers_also;
static const char *empty_directory;
static uint16_t answer_kind = ASTRA_VFS_KIND_FILE;
static uint32_t close_count;
static AstraVfsClient standin;
typedef struct TestLink {
    const char *path;
    const char *target;
} TestLink;
static TestLink links[4];
static uint32_t link_count;

/*
 * A second path this stand-in can be told to fail on, and the status to fail
 * it with -- so a test can put a specific non-NOT_FOUND status behind one
 * member's path without that member also being the one that succeeds.
 */
static const char *forced_error_path;
static uint32_t forced_error_status;

static void reset_asked(void)
{
    asked_count = 0u;
    stat_asked_count = 0u;
    link_count = 0u;
    close_count = 0u;
}

/*
 * The seam is astra_vfs_open: this file replaces it, so what is under test is
 * the loop and nothing beneath it.
 */
uint32_t
astra_vfs_open(AstraVfsClient *client, const char *path, uint32_t flags,
               AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    (void)client;
    if (asked_count < 8u) {
        strncpy(asked[asked_count], path, ASTRA_VFS_PATH_MAX - 1u);
        asked[asked_count][ASTRA_VFS_PATH_MAX - 1u] = '\0';
        ++asked_count;
    }
    if (forced_error_path != NULL && strcmp(path, forced_error_path) == 0) {
        return forced_error_status;
    }
    if ((answers == NULL || strcmp(path, answers) != 0) &&
        ((flags & ASTRA_VFS_OPEN_DIRECTORY) == 0u ||
         answers_also == NULL || strcmp(path, answers_also) != 0)) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (file != NULL) {
        *file = 42u;
    }
    if (size != NULL) {
        *size = 0u;
    }
    if (kind != NULL) {
        *kind = (flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u
                    ? ASTRA_VFS_KIND_DIRECTORY
                    : ASTRA_VFS_KIND_FILE;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_stat_meta(AstraVfsClient *client, const char *path,
                             AstraVfsDirEntry *entry)
{
    (void)client;
    if (stat_asked_count < 8u) {
        strncpy(stat_asked[stat_asked_count], path,
                ASTRA_VFS_PATH_MAX - 1u);
        stat_asked[stat_asked_count][ASTRA_VFS_PATH_MAX - 1u] = '\0';
        ++stat_asked_count;
    }
    if (forced_error_path != NULL && strcmp(path, forced_error_path) == 0)
        return forced_error_status;
    for (uint32_t index = 0u; index < link_count; ++index) {
        if (strcmp(path, links[index].path) == 0) {
            if (entry != NULL) {
                *entry = (AstraVfsDirEntry){0};
                entry->kind = ASTRA_VFS_KIND_SYMLINK;
                entry->size = strlen(links[index].target);
            }
            return ASTRA_VFS_OK;
        }
    }
    if ((answers == NULL || strcmp(path, answers) != 0) &&
        (answers_also == NULL || strcmp(path, answers_also) != 0))
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (entry != NULL) {
        *entry = (AstraVfsDirEntry){0};
        entry->kind = answer_kind;
        entry->size = 123u;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_readlink(AstraVfsClient *client, const char *path,
                            void *buffer, uint32_t capacity,
                            uint32_t *length)
{
    (void)client;
    for (uint32_t index = 0u; index < link_count; ++index) {
        size_t bytes;

        if (strcmp(path, links[index].path) != 0)
            continue;
        bytes = strlen(links[index].target);
        if (bytes > capacity)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        memcpy(buffer, links[index].target, bytes);
        *length = (uint32_t)bytes;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_readdir_batch(AstraVfsClient *client, const char *path,
                                 uint64_t cursor, AstraVfsDirEntry *entries,
                                 uint32_t capacity, uint32_t *count,
                                 uint64_t *next)
{
    (void)client;
    if (entries == NULL || capacity == 0u || count == NULL || next == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *count = 0u;
    *next = 0u;
    if (empty_directory != NULL && strcmp(path, empty_directory) == 0)
        return ASTRA_VFS_OK;
    if (cursor != 0u)
        return ASTRA_VFS_OK;
    if (strcmp(path, "/local/commands") == 0)
        strcpy(entries[0].name, "local");
    else if (strcmp(path, "/commands") == 0)
        strcpy(entries[0].name, "base");
    else
        return ASTRA_VFS_ERR_NOT_FOUND;
    entries[0].kind = ASTRA_VFS_KIND_FILE;
    *count = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_readdir_file_batch(
    AstraVfsClient *client, AstraVfsFile directory, const char *path,
    uint64_t cursor, AstraVfsDirEntry *entries, uint32_t capacity,
    uint32_t *count, uint64_t *next)
{
    assert(directory == 42u);
    return astra_vfs_readdir_batch(client, path, cursor, entries, capacity,
                                   count, next);
}

uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    assert(client == &standin);
    assert(file == 42u);
    ++close_count;
    return ASTRA_VFS_OK;
}

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
    reset_asked();
    forced_error_path = NULL;
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
    reset_asked();
    forced_error_path = NULL;
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
    reset_asked();
    forced_error_path = NULL;
    answers = NULL;
    assert(astra_vfs_assign_open(&table, "COMMANDS:nothing", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(asked_count == 2u);

    /* A name nobody bound is refused without asking any disk anything. */
    reset_asked();
    forced_error_path = NULL;
    assert(astra_vfs_assign_open(&table, "APPS:status", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(asked_count == 0u);
}

static void
test_creation_goes_to_the_primary_member(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    reset_asked();
    forced_error_path = NULL;
    answers = "/local/commands/new";
    /*
     * Creation goes to the primary, which is the first member holding write
     * rights -- not a stored field, but a consequence of a fixed order. Both
     * members here can serve a write, so this proves the primary is reached
     * first; test_a_writable_member_that_is_second_still_answers is the one
     * that proves a rights-deficient earlier member gets skipped.
     */
    assert(astra_vfs_assign_open(&table, "COMMANDS:new", ASTRA_RIGHT_WRITE,
                                 ASTRA_VFS_OPEN_WRITE, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_OK);
    assert(member == 0u);
    assert(asked_count == 1u);
}

/*
 * A second topology, deliberately the reverse of union_table's: member 0
 * carries only read rights and member 1 carries read and write. Nothing in
 * union_table can tell "the loop skipped a rights-deficient member" apart
 * from "the loop always answers from member 0", because member 0 there always
 * has the rights. This table can.
 */
static void
union_table_write_second(AstraAssignTable *table)
{
    astra_assign_table_init(table);
    assert(astra_assign_bind(table, "COMMANDS", 9u, ASTRA_RIGHT_READ,
                             "commands") == ASTRA_VFS_OK);
    assert(astra_assign_join(table, "COMMANDS", 9u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "local/commands") == ASTRA_VFS_OK);
}

static void
test_a_writable_member_that_is_second_still_answers(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table_write_second(&table);
    reset_asked();
    forced_error_path = NULL;
    answers = "/local/commands/report";
    /*
     * Member 0 carries only read rights, so astra_assign_resolve refuses it
     * for a write before the loop ever calls a client -- that refusal is a
     * string check, not I/O. A regression that turned that "try the next
     * member" continue into a break would return NOT_FOUND here instead of
     * reaching member 1, and asked_count would still read 0 rather than 1.
     */
    assert(astra_vfs_assign_open(&table, "COMMANDS:report", ASTRA_RIGHT_WRITE,
                                 ASTRA_VFS_OPEN_WRITE, client_for, NULL, wire,
                                 sizeof(wire), &file, NULL, NULL, &client,
                                 &member) == ASTRA_VFS_OK);
    assert(member == 1u);
    /* Member 0 never reached astra_vfs_open at all. */
    assert(asked_count == 1u);
    assert(strcmp(asked[0], "/local/commands/report") == 0);
}

/*
 * Both members of union_table share a handle (9u), so a stand-in that refused
 * by handle could not single one out. Refusing by root can: it is the one
 * thing that actually differs between the two members here.
 */
static AstraVfsClient *
client_for_no_client_on_local_commands(const AstraAssign *assign,
                                       void *context)
{
    (void)context;
    if (strcmp(assign->root, "local/commands") == 0) {
        return NULL;
    }
    return assign->handle != 0u ? &standin : NULL;
}

static void
test_a_member_no_client_serves_is_skipped(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table(&table);
    reset_asked();
    forced_error_path = NULL;
    answers = "/commands/status";
    /*
     * Member 0 resolves fine -- this is not a rights refusal -- but nothing
     * serves it. A regression that turned this "no client, try the next"
     * continue into a break would return NOT_FOUND without ever reaching
     * member 1, and astra_vfs_open would never have been called at all.
     */
    assert(astra_vfs_assign_open(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                 ASTRA_VFS_OPEN_READ,
                                 client_for_no_client_on_local_commands, NULL,
                                 wire, sizeof(wire), &file, NULL, NULL,
                                 &client, &member) == ASTRA_VFS_OK);
    assert(member == 1u);
    assert(asked_count == 1u);
    assert(strcmp(asked[0], "/commands/status") == 0);
}

static void
test_a_disk_error_outranks_a_later_not_found(void)
{
    AstraAssignTable table;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;
    uint32_t status;

    union_table(&table);
    reset_asked();
    answers = NULL;
    forced_error_path = "/local/commands/status";
    forced_error_status = ASTRA_VFS_ERR_IO;
    /*
     * Member 0's open fails with an I/O error and member 1 genuinely lacks
     * the file. The loop must still try both -- a union recovers around a
     * broken member -- but what it reports when nothing answers must be the
     * I/O failure, not member 1's plain NOT_FOUND: a caller told "not found"
     * when a device actually failed has no reason to stop retrying against a
     * machine that cannot ever answer.
     */
    status = astra_vfs_assign_open(&table, "COMMANDS:status", ASTRA_RIGHT_READ,
                                   ASTRA_VFS_OPEN_READ, client_for, NULL, wire,
                                   sizeof(wire), &file, NULL, NULL, &client,
                                   &member);
    assert(status == ASTRA_VFS_ERR_IO);
    assert(stat_asked_count == 2u);
    assert(strcmp(stat_asked[0], "/local/commands/status") == 0);
    assert(strcmp(stat_asked[1], "/commands/status") == 0);
    forced_error_path = NULL;
}

static void test_primary_skips_a_read_only_member(void)
{
    AstraAssignTable table;
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table_write_second(&table);
    assert(astra_vfs_assign_primary(
               &table, "COMMANDS:new", ASTRA_RIGHT_WRITE, client_for, NULL,
               wire, sizeof(wire), &client, &member) == ASTRA_VFS_OK);
    assert(client == &standin);
    assert(member == 1u);
    assert(strcmp(wire, "/local/commands/new") == 0);
}

static void test_stat_checks_rights_after_finding_the_name(void)
{
    AstraAssignTable table;
    AstraVfsClient *client = NULL;
    AstraVfsDirEntry entry;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 99u;

    union_table_write_second(&table);
    reset_asked();
    answers = "/commands/status";
    answers_also = "/local/commands/status";
    assert(astra_vfs_assign_stat(
               &table, "COMMANDS:status", ASTRA_RIGHT_WRITE, client_for,
               NULL, wire, sizeof(wire), &entry, &client, NULL, &member) ==
           ASTRA_VFS_OK);
    assert(stat_asked_count == 2u);
    assert(member == 1u);
    assert(entry.size == 123u);
    assert(client == &standin);
    assert(strcmp(wire, "/local/commands/status") == 0);
    answers_also = NULL;
}

static void test_directory_skips_an_empty_member(void)
{
    AstraAssignTable table;
    AstraVfsUnionDirectory directory = ASTRA_VFS_UNION_DIRECTORY_INIT;
    AstraVfsDirEntry entry;
    uint32_t count = 0u;
    uint32_t member = 99u;

    union_table(&table);
    answers = "/local/commands";
    answers_also = "/commands";
    empty_directory = "/local/commands";
    answer_kind = ASTRA_VFS_KIND_DIRECTORY;
    assert(astra_vfs_union_directory_open(
               &table, "COMMANDS:", client_for, NULL, &directory) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_union_directory_read(
               &directory, &entry, 1u, &count, &member) == ASTRA_VFS_OK);
    assert(count == 1u);
    assert(member == 1u);
    assert(strcmp(entry.name, "base") == 0);
    assert(astra_vfs_union_directory_read(
               &directory, &entry, 1u, &count, &member) == ASTRA_VFS_OK);
    assert(count == 0u);
    astra_vfs_union_directory_close(&directory);
    assert(directory.active == 0u);
    assert(close_count == 2u);
    answers_also = NULL;
    empty_directory = NULL;
    answer_kind = ASTRA_VFS_KIND_FILE;
}

static void test_links_follow_without_crossing_authority(void)
{
    AstraAssignTable table;
    AstraVfsDirEntry entry;
    char wire[ASTRA_VFS_PATH_MAX];

    astra_assign_table_init(&table);
    assert(astra_assign_bind(&table, "WORK", 9u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "work") == ASTRA_VFS_OK);
    reset_asked();
    links[0] = (TestLink){"/work/link", "file"};
    link_count = 1u;
    answers = "/work/file";
    answers_also = NULL;
    assert(astra_vfs_assign_stat(
               &table, "WORK:link", ASTRA_RIGHT_READ, client_for, NULL,
               wire, sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(entry.kind == ASTRA_VFS_KIND_FILE);
    assert(strcmp(wire, "/work/file") == 0);

    links[0] = (TestLink){"/work/link", "../secret"};
    assert(astra_vfs_assign_stat(
               &table, "WORK:link", ASTRA_RIGHT_READ, client_for, NULL,
               wire, sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    links[0] = (TestLink){"/work/link", "SECRET:secret"};
    assert(astra_vfs_assign_stat(
               &table, "WORK:link", ASTRA_RIGHT_READ, client_for, NULL,
               wire, sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    links[0] = (TestLink){"/work/a", "b"};
    links[1] = (TestLink){"/work/b", "a"};
    link_count = 2u;
    assert(astra_vfs_assign_stat(
               &table, "WORK:a", ASTRA_RIGHT_READ, client_for, NULL,
               wire, sizeof(wire), &entry, NULL, NULL, NULL) ==
           ASTRA_VFS_ERR_LOOP);
    link_count = 0u;
}

int
main(void)
{
    test_the_first_member_that_opens_wins();
    test_a_member_that_does_not_have_it_is_skipped();
    test_no_member_has_it();
    test_creation_goes_to_the_primary_member();
    test_a_writable_member_that_is_second_still_answers();
    test_a_member_no_client_serves_is_skipped();
    test_a_disk_error_outranks_a_later_not_found();
    test_primary_skips_a_read_only_member();
    test_stat_checks_rights_after_finding_the_name();
    test_directory_skips_an_empty_member();
    test_links_follow_without_crossing_authority();
    puts("ASTRA VFS UNION PASS");
    return 0;
}
