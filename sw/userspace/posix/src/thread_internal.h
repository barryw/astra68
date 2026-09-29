#ifndef ASTRA_POSIX_THREAD_INTERNAL_H
#define ASTRA_POSIX_THREAD_INTERNAL_H

/* A forked child is a new thread with a new handle; forget the parent's. */
void astra_posix_thread_after_fork_child(void);

#endif
