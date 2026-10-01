#ifndef ASTRA_POSIX_SIGNAL_INTERNAL_H
#define ASTRA_POSIX_SIGNAL_INTERNAL_H

#include <stdint.h>

/*
 * astra_wait_one for a blocking POSIX call. The kernel cancels a wait for
 * any signal delivery, but POSIX interrupts a call (EINTR) only when a
 * handler ran: an ignored signal, or a default-ignored one such as the
 * SIGCHLD of a child exiting, resumes the wait.
 */
uint32_t posix_wait_one(uint32_t handle, uint64_t deadline);

#endif
