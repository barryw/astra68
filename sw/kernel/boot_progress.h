#ifndef ASTRA_KERNEL_BOOT_PROGRESS_H
#define ASTRA_KERNEL_BOOT_PROGRESS_H

#include <stdint.h>

/*
 * The kernel's half of the boot progress record (astra/boot_progress.h).
 * The ROM creates it and counts its own steps; from handoff the kernel is
 * its only writer. Without a ROM record or a text page nothing is written,
 * but the value is still tracked so commands answer the same.
 */
void kernel_boot_progress_init(void);
uint32_t kernel_boot_progress_value(void);
/* One of the kernel's ASTRA_BOOT_PROGRESS_KERNEL_STEPS. */
void kernel_boot_progress_step(void);
/* A supervisor stage reached: 1-7 are fixed steps, 8 finishes, 9 fails. */
void kernel_boot_progress_stage(uint32_t stage);
void kernel_boot_progress_fail(void);
/* An ASTRA_PROGRESS_BOOT_* command; returns an ASTRA_SYSCALL_* status. */
uint32_t kernel_boot_progress_command(uint32_t command);

#endif
