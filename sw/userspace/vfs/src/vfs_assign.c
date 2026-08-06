/*
 * The assign table.
 *
 * Names are canonicalised to uppercase on the way in and compared exactly
 * afterwards, so `work:` and `WORK:` are one binding: assign names are a small
 * closed set typed by people, and a typo must not create a second namespace.
 * Everything after the colon is byte-exact and is not this file's business.
 *
 * Nothing here allocates and nothing here resolves. A table is a caller-owned
 * array of names and the authority each one stands for; turning a name into a
 * file is the client Kit's job, and it cannot start until this answers.
 */

#include <astra/vfs_assign.h>

#include <stddef.h>

static char
upper(char value)
{
    return (value >= 'a' && value <= 'z') ? (char)(value - ('a' - 'A')) : value;
}

/*
 * A name is A-Z, 0-9 and underscore. The colon is the separator, so it cannot
 * appear inside one; a space would make a name nobody could type back at the
 * machine; anything else is a name the shell would have to quote.
 */
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
    uint32_t index;

    for (index = 0u; index < ASTRA_CAPABILITY_NAME_MAX; ++index) {
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
    uint32_t index;

    if (table == NULL) {
        return;
    }
    for (index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
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
    uint32_t index;

    /*
     * A handle of zero or rights of zero would bind a name that resolves and
     * then fails, which is worse than a name that does not resolve: the caller
     * learns it has no authority one operation later than it should.
     */
    if (table == NULL || handle == 0u || rights == 0u ||
        !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (index = 0u; index < table->count; ++index) {
        if (same(table->entries[index].name, canonical_name)) {
            table->entries[index].handle = handle;
            table->entries[index].rights = rights;
            return ASTRA_VFS_OK;
        }
    }
    if (table->count >= ASTRA_ASSIGN_MAX) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    for (index = 0u; index < ASTRA_CAPABILITY_NAME_MAX; ++index) {
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
    uint32_t index;

    if (table == NULL || !canonical(name, canonical_name)) {
        return NULL;
    }
    for (index = 0u; index < table->count; ++index) {
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
    uint32_t index;

    if (table == NULL || !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (index = 0u; index < table->count; ++index) {
        if (!same(table->entries[index].name, canonical_name)) {
            continue;
        }
        /*
         * The last entry moves into the hole. Order is not a property of a
         * namespace -- a name means what it is bound to, not where it sits --
         * so compacting costs nothing and keeps lookup linear over `count`.
         */
        table->entries[index] = table->entries[table->count - 1u];
        --table->count;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}
