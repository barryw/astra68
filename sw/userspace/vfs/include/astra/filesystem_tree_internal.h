#ifndef ASTRA_FILESYSTEM_TREE_INTERNAL_H
#define ASTRA_FILESYSTEM_TREE_INTERNAL_H

#include <astra/filesystem_library.h>

/* Shared private traversal used by staged-copy cleanup, cross-volume move,
 * and Trash. Rejects an assign root; may leave a partially removed tree when
 * an underlying operation fails. */
uint32_t astra_filesystem_remove_tree_internal(AstraFilesystem *filesystem,
    const char *path);

#endif
