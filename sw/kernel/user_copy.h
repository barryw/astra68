#ifndef ASTRA_KERNEL_USER_COPY_H
#define ASTRA_KERNEL_USER_COPY_H

#define KERNEL_USER_COPY_OK 0
#define KERNEL_USER_COPY_BAD_ADDRESS 1
#define KERNEL_USER_COPY_INVALID_ARGUMENT 2
#define KERNEL_USER_COPY_TOO_LARGE 3
#define KERNEL_USER_COPY_BUSY 4

#define KERNEL_USER_COPY_MAX_BYTES 4096u
#define KERNEL_USER_COPY_FROM_USER 1u
#define KERNEL_USER_COPY_TO_USER 2u

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stdint.h>

typedef struct KernelUserCopyFaultScope {
    uint32_t start;
    uint32_t end;
    uint32_t direction;
    uint32_t active;
} KernelUserCopyFaultScope;

typedef struct KernelUserCopyFaultSite {
    uint32_t start_pc;
    uint32_t end_pc;
    uint32_t fixup_pc;
    uint32_t direction;
} KernelUserCopyFaultSite;

int kernel_copy_from_user(void *kernel_destination, uint32_t user_source,
                          uint32_t size);
int kernel_copy_to_user(uint32_t user_destination, const void *kernel_source,
                        uint32_t size);
bool kernel_user_copy_handle_fault(void *raw_frame);
bool kernel_user_copy_recover_frame(
    void *raw_frame, uint32_t available,
    const KernelUserCopyFaultScope *scope,
    const KernelUserCopyFaultSite *sites, uint32_t site_count);

/*
 * Commits grow-on-touch user pages covering the range for the running thread.
 * Implemented by the process layer, which owns stack and private reservations;
 * the copy path can meet either before the user takes the fault that would
 * ordinarily commit it.
 */
bool kernel_process_prepare_user_copy(uint32_t address, uint32_t size,
                                      bool write);

int kernel_user_copy_from_asm(void *kernel_destination, uint32_t user_source,
                              uint32_t size);
int kernel_user_copy_to_asm(uint32_t user_destination,
                            const void *kernel_source, uint32_t size);

#endif
#endif
