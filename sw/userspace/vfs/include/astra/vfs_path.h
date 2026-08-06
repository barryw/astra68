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

#endif
