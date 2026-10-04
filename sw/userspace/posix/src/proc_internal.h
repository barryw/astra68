#ifndef ASTRA_POSIX_PROC_INTERNAL_H
#define ASTRA_POSIX_PROC_INTERNAL_H

#include <stdint.h>
#include <sys/types.h>

/*
 * Another process is reached through PROC:, the supervisor's process view,
 * the same way for a shell job, a desktop application or a service: its
 * status file reports, its ctl file obeys (sw/userspace/supervisor/
 * include/proc_tree.h has the commands). Both return 0, or -1 with errno:
 * ENOSYS when the caller has no PROC: mount, ENOENT when there is no such
 * process, EACCES when the caller may not or the target is protected, and
 * otherwise what open/read/write left.
 */
int posix_proc_control(pid_t process, const char *command);
int posix_proc_priority(pid_t process, uint32_t *priority);

#endif
