/** @file vfs_path.h @brief Assign-qualified path parsing and validation. */
#ifndef ASTRA_VFS_PATH_H
#define ASTRA_VFS_PATH_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

/** Native assign-name storage including NUL. Filesystem mount names are
 * independent of the smaller startup capability-name field. */
#define ASTRA_ASSIGN_NAME_MAX 32u

/**
 * Split /name/rest into an assign name and its byte-exact remainder. The
 * name is canonicalised to uppercase; Makefile and makefile remain distinct.
 * The Filesystem Kit resolves dot components and links in order and permits
 * namespace continuation only through assigns held by the process.
 * @param path NUL-terminated assign-qualified path.
 * @param name Receives the uppercase assign name without separators.
 * @param name_capacity Bytes available in `name`.
 * @param rest Receives the mount-relative path.
 * @param rest_capacity Bytes available in `rest`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_path_split(const char *path, char *name, uint32_t name_capacity,
                          char *rest, uint32_t rest_capacity);

/** Paths with dot components or a trailing separator require ordered lookup.
 * @param path Path spelling to inspect.
 * @return Nonzero if the spelling cannot use an ordinary backend lookup.
 */
int astra_path_requires_walk(const char *path);

/**
 * Normalize separators and dot components without permitting root escape.
 * @param rest Mount-relative input path.
 * @param out Receives the normalized path.
 * @param capacity Bytes available in `out`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_path_normalise(const char *rest, char *out, uint32_t capacity);

/**
 * What a word typed at a prompt means, given where the shell is standing.
 *
 * A word beginning with / is absolute and is copied; anything else is joined
 * onto the current assign and directory. The
 * result is still only a string -- it is astra_assign_resolve() that decides
 * whether the process holds what it names.
 * @param assign Current assign name.
 * @param directory Current mount-relative directory.
 * @param typed User-supplied qualified or relative path.
 * @param out Receives an assign-qualified path.
 * @param capacity Bytes available in `out`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_path_qualify(const char *assign, const char *directory,
                            const char *typed, char *out, uint32_t capacity);

/** Qualify a live provider directory beneath a rooted assign.
 * @param assign Namespace name. @param root Assign's provider-relative root.
 * @param directory Live provider directory path. @param typed Relative path.
 * @param out Receives namespace spelling. @param capacity Output capacity.
 * @return ASTRA_VFS_* status, NOT_FOUND when directory lies outside root. */
uint32_t astra_path_qualify_provider(const char *assign, const char *root,
    const char *directory, const char *typed, char *out, uint32_t capacity);

/** Collapse repeated separators without folding dot components.
 * @param path UTF-8 input path.
 * @param out Receives the preserved path.
 * @param capacity Output capacity including the terminator.
 * @return ASTRA_VFS_* status. */
uint32_t astra_path_collapse(const char *path, char *out, uint32_t capacity);

#endif
