#ifndef ASTRA_TLS_H
#define ASTRA_TLS_H

/** @file tls.h
 * @brief MC68040 thread-local-storage ABI constants.
 *
 * Astra reserves address register A4 as the thread pointer.  The kernel,
 * dynamic loader, C library, and compiler support code consume this one
 * definition so independently built components cannot disagree about the
 * offset between a thread's TLS allocation and A4.
 */

/** Byte offset from the start of a thread's TLS allocation to its A4 value. */
#define ASTRA_M68K_TLS_THREAD_POINTER_BIAS 0x7000u

#endif
