#ifndef ASTRA_VFS_ASSIGN_H
#define ASTRA_VFS_ASSIGN_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

/*
 * A process's namespace: the names it may use, and the authority each one
 * stands for.
 *
 * An assign is not a shortcut to a path. It is a name for a capability the
 * process was handed, so a name it does not hold cannot be reached by spelling
 * it correctly -- which is the property the whole namespace design rests on.
 *
 * Sixteen entries, because that is the startup capability table's own limit: a
 * namespace larger than the grant that seeds it cannot arise.
 *
 * Names are canonicalised to uppercase here and compared exactly afterwards.
 * `work:` and `WORK:` are one binding, because assign names are a small closed
 * set typed by people and a typo must not create a second namespace. What
 * follows the colon is byte-exact and is not this file's business.
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

/*
 * Binds a name, replacing any binding it already had: a name has one meaning
 * at a time. Refuses a handle of zero or rights of zero, because a binding
 * that confers nothing is a name that would resolve and then fail.
 */
uint32_t astra_assign_bind(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights);

const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name);

uint32_t astra_assign_unbind(AstraAssignTable *table, const char *name);

#endif
