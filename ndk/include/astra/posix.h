#ifndef ASTRA_POSIX_H
#define ASTRA_POSIX_H

#include <stddef.h>
#include <stdint.h>

#include <astra/process.h>

/*
 * The POSIX layer, and what it is for.
 *
 * Astra has no file descriptors, no current directory and no root. It has
 * capabilities, assigns and a VFS spoken over ports. Unix software assumes the
 * opposite of all three, so something has to stand between them, and this is
 * it: the smallest surface that lets picolibc -- and after it, ported Unix
 * programs -- run without the kernel growing a Unix personality.
 *
 * The rule this file keeps: a descriptor is an index into a table this process
 * owns, and every entry holds a capability the process was already granted.
 * Nothing here invents authority. A program that was not handed STDOUT has no
 * fd 1, and that is the correct answer rather than a missing feature.
 *
 * `astra_posix_start` must run before any stdio call. Programs with Astra's
 * native `astra_main` entry call it explicitly; ordinary C programs get it
 * from the POSIX library's `main` adapter when that archive member is needed.
 */
void astra_posix_start(const AstraStartupInfo *startup);

/* Native capabilities remain available to POSIX-entry Astra programs. */
const AstraStartupInfo *astra_posix_startup(void);

/* The process/session service granted at startup, or zero when unavailable. */
uint32_t astra_posix_process_service(void);
uint32_t astra_posix_signal_generation(void);

/* Completes a POSIX write across short transfers and signal interruption. */
int astra_posix_write_all(int descriptor, const void *bytes, size_t length);

#endif
