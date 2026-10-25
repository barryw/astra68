#ifndef ASTRA_VFS_ASSIGN_INTERNAL_H
#define ASTRA_VFS_ASSIGN_INTERNAL_H

#include <astra/vfs_assign.h>

/* Canonical namespace retirement for the provider owner. Preserve ordered
 * members belonging to other authorities, even within the same union. */
uint32_t astra_vfs_assign_retire_handle(AstraAssignTable *table, uint32_t handle);

uint32_t astra_vfs_assign_clone(AstraAssignTable *target, const AstraAssignTable *source);

#endif
