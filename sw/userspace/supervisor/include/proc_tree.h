#ifndef ASTRA_SUPERVISOR_PROC_TREE_H
#define ASTRA_SUPERVISOR_PROC_TREE_H

#include <astra/vfs_backend.h>

/*
 * The backend behind the PROC: assign. Rendered from the supervisor's own
 * process handles, because holding them is what makes an answer possible --
 * see proc_tree.c and docs/OBSERVABILITY.md.
 */
const AstraVfsBackendOps *supervisor_proc_ops(void);

#endif
