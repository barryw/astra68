#ifndef ASTRA_RUNTIME_H
#define ASTRA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <astra/process.h>

typedef struct AstraSyscallResult {
    uint32_t status;
    uint32_t value0;
    uint32_t value1;
    uint32_t value2;
} AstraSyscallResult;

_Static_assert(sizeof(AstraSyscallResult) == 16u,
               "syscall-result layout changed");

int astra_startup_validate(const AstraStartupInfo *startup);

void astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
                    uint32_t argument2, uint32_t argument3,
                    uint32_t argument4, AstraSyscallResult *result);

uint32_t astra_yield(void);
uint32_t astra_close(uint32_t handle);
uint32_t astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                         uint32_t *thread_handle);
void astra_process_exit(uint32_t status) __attribute__((noreturn));
void astra_thread_exit(uint32_t status) __attribute__((noreturn));

int astra_main(const AstraStartupInfo *startup);

#endif
