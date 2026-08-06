#ifndef ASTRA_VFS_ASSIGN_H
#define ASTRA_VFS_ASSIGN_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_path.h>
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

/*
 * Where an assign begins inside its mount. Roots are short by nature -- work,
 * apps, sys/commands -- and this is per entry, so the bound is the difference
 * between a namespace that costs one kilobyte and one that costs three.
 */
#define ASTRA_ASSIGN_ROOT_MAX 64u

typedef struct AstraAssign {
    char     name[ASTRA_CAPABILITY_NAME_MAX];  /* canonical uppercase */
    /*
     * Normalised and mount-relative, with no leading separator: "work", or ""
     * for the mount's own root. Stored in the form it is joined in, so
     * resolution is a copy rather than a parse.
     */
    char     root[ASTRA_ASSIGN_ROOT_MAX];
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
 * that confers nothing is a name that would resolve and then fail. `root` is
 * normalised on the way in and may be "" for the mount's own root.
 */
uint32_t astra_assign_bind(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights, const char *root);

const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name);

uint32_t astra_assign_unbind(AstraAssignTable *table, const char *name);

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

#endif
