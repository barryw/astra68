#ifndef ASTRA_VFS_PATH_H
#define ASTRA_VFS_PATH_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

/*
 * Paths on this machine are NAME:rest and there is no root. The absence of one
 * is a security property rather than an aesthetic choice: there is nothing to
 * enumerate, and `..` at an assign's root is an error rather than a parent, so
 * no string a program can build escapes the authority it was given.
 *
 * The assign name is canonicalised to uppercase. Everything after the colon is
 * byte-exact: Makefile and makefile are two files.
 */
uint32_t astra_path_split(const char *path, char *name, uint32_t name_capacity,
                          char *rest, uint32_t rest_capacity);

uint32_t astra_path_normalise(const char *rest, char *out, uint32_t capacity);

/*
 * What a word typed at a prompt means, given where the shell is standing.
 *
 * A word whose first component carries a colon is already absolute and is
 * copied; anything else is joined onto the current assign and directory. The
 * result is still only a string -- it is astra_assign_resolve() that decides
 * whether the process holds what it names.
 */
uint32_t astra_path_qualify(const char *assign, const char *directory,
                            const char *typed, char *out, uint32_t capacity);

#endif
