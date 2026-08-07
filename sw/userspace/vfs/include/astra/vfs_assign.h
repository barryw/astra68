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
 * namespace larger than the grant that seeds it cannot arise. It counts
 * *members* rather than names -- a member is a binding, and a union is two
 * bindings that share a name.
 *
 * Names are canonicalised to uppercase here and compared exactly afterwards.
 * `work:` and `WORK:` are one binding, because assign names are a small closed
 * set typed by people and a typo must not create a second namespace. What
 * follows the colon is byte-exact and is not this file's business.
 */
#define ASTRA_ASSIGN_MAX 16u

/*
 * Where an assign begins inside its mount. This is the grant's root field and
 * nothing else: two constants for one limit is the mistake that cost four
 * tasks the last time, so there is one.
 */
#define ASTRA_ASSIGN_ROOT_MAX ASTRA_CAPABILITY_ROOT_MAX

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

/*
 * Builds a namespace out of what a launch handed over.
 *
 * A launched program's capability table *is* its namespace: there is no
 * manifest to read and no path to search, so the names it may use are exactly
 * the ones somebody granted it, under the names that somebody chose. This is
 * the whole of that translation, and it replaces the table rather than adding
 * to it -- a namespace is what a process was given, not an accumulation.
 *
 * **Only a grant carrying ASTRA_CAPABILITY_FLAG_NAMESPACE becomes a name.** The
 * rule is positive on purpose. A capability table publishes every kind of
 * authority a process holds and they are not all names: `WORK:src/main.c` means
 * something and `STDOUT:src/main.c` is nonsense. The alternative is a list here
 * of capabilities that are not mounts, which grows every time a new kind is
 * invented and lives in a file that has nothing to do with any of them. PROCESS
 * and THREAD are excluded by construction rather than by being remembered: the
 * kernel installs them carrying no flags.
 *
 * An entry that declared itself a name and is not one -- a name the shell could
 * not type back, or one carrying no rights -- is skipped rather than fatal, so
 * one bad grant cannot cost a child the names it was actually given. Running
 * out of room is the one thing reported, because a namespace quietly missing
 * its tail is a program failing later for a reason nothing wrote down.
 *
 * The root travels in the record and is bound with the name, so a child's
 * COMMANDS: means the directory it was granted rather than the whole volume.
 * A name granted twice is a union: the first record binds and each later one
 * joins, in the order the launcher listed them.
 */
uint32_t astra_assign_seed(AstraAssignTable *table,
                           const AstraStartupCapability *capabilities,
                           uint32_t count);

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

#endif
