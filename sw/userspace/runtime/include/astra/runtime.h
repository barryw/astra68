#ifndef ASTRA_RUNTIME_H
#define ASTRA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <astra/block.h>
#include <astra/input.h>
#include <astra/process.h>
#include <astra/syscall.h>

/*
 * Exit status of a process that failed an assertion, tagged the same way the
 * supervisor tags its own result: "AS" in the high halfword, the failing
 * source line in the low one.
 */
#define ASTRA_ASSERT_STATUS_TAG 0x41530000u

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
uint32_t astra_process_info(uint32_t handle, AstraProcessInfo *info);
uint32_t astra_progress(uint32_t value);
uint32_t astra_device_query(uint32_t handle, AstraDeviceInfo *info);
uint32_t astra_device_reset(uint32_t handle);
uint64_t astra_clock_monotonic(void);
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns,
                        uint32_t *detail);
uint32_t astra_irq_arm(uint32_t handle);
uint32_t astra_irq_read(uint32_t handle, AstraIrqRecord *record,
                        uint32_t *events);
uint32_t astra_irq_ack(uint32_t handle, uint32_t sequence);
uint32_t astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info);
uint32_t astra_block_lease_query(uint32_t device, AstraBlockLeaseInfo *geometry);
uint32_t astra_block_lease_submit(uint32_t device, const AstraBlockRequest *request,
                            uint32_t *block_request);
uint32_t astra_block_lease_collect(uint32_t device, uint32_t block_request,
                             AstraBlockCompletion *completion);
uint32_t astra_console_info(uint32_t device, uint32_t *columns,
                            uint32_t *rows);
uint32_t astra_console_write(uint32_t device, uint32_t cell,
                             const uint8_t *cells, uint32_t count);
uint32_t astra_input_read(uint32_t device, AstraInputEvent *events,
                          uint32_t capacity, uint32_t *count,
                          uint32_t *flags);
void astra_process_exit(uint32_t status) __attribute__((noreturn));

/*
 * The diagnostic channel. Bound once from the startup block, then usable from
 * anywhere without threading a handle through every call site.
 *
 * Every one of these can be refused -- a build without a debug surface grants
 * no process the right to write -- so the status is returned and never acted
 * on here. Diagnostics that a program depends on are a program that stops
 * working when the diagnostics are turned off.
 */
void astra_log_bind(uint32_t process_handle);
uint32_t astra_log_handle(void);
uint32_t astra_log_write(const void *bytes, uint32_t length);
uint32_t astra_log(const char *text);
uint32_t astra_assert_message(char *out, uint32_t capacity, const char *file,
                              uint32_t line, const char *expression);

/* Declared identically by the freestanding <assert.h> vendored code sees. */
void astra_assert_failed(const char *file, unsigned int line,
                         const char *expression) __attribute__((noreturn));
void astra_thread_exit(uint32_t status) __attribute__((noreturn));

int astra_main(const AstraStartupInfo *startup);

#endif
