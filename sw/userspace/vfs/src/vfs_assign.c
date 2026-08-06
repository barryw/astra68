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

/* Copies and pads, so an entry never carries bytes of the name before it. */
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

void
astra_assign_table_init(AstraAssignTable *table)
{
    uint32_t index;

    if (table == NULL) {
        return;
    }
    for (index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        table->entries[index].name[0] = '\0';
        table->entries[index].root[0] = '\0';
        table->entries[index].handle = 0u;
        table->entries[index].rights = 0u;
    }
    table->count = 0u;
}

uint32_t
astra_assign_bind(AstraAssignTable *table, const char *name, uint32_t handle,
                  uint32_t rights, const char *root)
{
    char canonical_name[ASTRA_CAPABILITY_NAME_MAX];
    char canonical_root[ASTRA_ASSIGN_ROOT_MAX];
    uint32_t index;

    /*
     * A handle of zero or rights of zero would bind a name that resolves and
     * then fails, which is worse than a name that does not resolve: the caller
     * learns it has no authority one operation later than it should.
     */
    if (table == NULL || handle == 0u || rights == 0u || root == NULL ||
        !canonical(name, canonical_name)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /*
     * The root goes through the path parser rather than growing rules of its
     * own. Binding is where authority is handed over, so it is the last place
     * a root that climbs out of its mount can be refused -- everything
     * downstream of here trusts it.
     */
    if (astra_path_normalise(root, canonical_root, sizeof(canonical_root)) !=
        ASTRA_VFS_OK) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (index = 0u; index < table->count; ++index) {
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

uint32_t
astra_assign_seed(AstraAssignTable *table,
                  const AstraStartupCapability *capabilities, uint32_t count)
{
    uint32_t index;

    if (table == NULL || (count != 0u && capabilities == NULL) ||
        count > ASTRA_STARTUP_CAPABILITY_MAX) {
        return ASTRA_VFS_ERR_INVALID;
    }
    astra_assign_table_init(table);
    for (index = 0u; index < count; ++index) {
        /*
         * The name is copied out and terminated here: the published field is
         * fixed-width bytes and is not a C string until somebody says so. The
         * kernel terminates at the same place, so a name too long to carry is
         * the same name on both sides of the launch.
         */
        char name[ASTRA_CAPABILITY_NAME_MAX];

        copy(name, capabilities[index].name, ASTRA_CAPABILITY_NAME_MAX);
        if (same(name, ASTRA_CAPABILITY_PROCESS) ||
            same(name, ASTRA_CAPABILITY_THREAD)) {
            continue;
        }
        if (astra_assign_bind(table, name, capabilities[index].handle,
                              capabilities[index].rights,
                              "") == ASTRA_VFS_ERR_LIMIT) {
            return ASTRA_VFS_ERR_LIMIT;
        }
    }
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

uint32_t
astra_assign_resolve(const AstraAssignTable *table, const char *path,
                     uint32_t rights, char *wire, uint32_t capacity,
                     const AstraAssign **assign)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    /*
     * The one scratch buffer in the namespace path, and a frame the deep chain
     * never sees: resolution finishes before the client call it feeds, so this
     * is not paid on top of the service, the backend and lwext4 underneath.
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

    /* The mount-absolute prefix: "/" and then the assign's own root. */
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
     * backtracks within the buffer it was given, so it cannot walk back over
     * the root: the `..` refusal and the assign boundary are one mechanism
     * rather than two that have to agree with each other.
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
