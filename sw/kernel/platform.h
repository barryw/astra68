#ifndef ASTRA_KERNEL_PLATFORM_H
#define ASTRA_KERNEL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct KernelPlatformBlockState {
    uint32_t capabilities;
    uint32_t state_flags;
    uint32_t media_generation;
    uint32_t host_generation;
    uint64_t media_sectors;
    uint16_t max_sectors;
    uint16_t reserved;
} KernelPlatformBlockState;

typedef struct KernelPlatformBlockCompletion {
    uint32_t id;
    uint16_t status;
    uint16_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} KernelPlatformBlockCompletion;

typedef struct KernelInputEvent {
    uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
    uint32_t host_generation;
} KernelInputEvent;

void kernel_platform_interrupt_init(uint32_t cpu_hz);
uint32_t kernel_platform_ticks(void);
uint32_t kernel_platform_cpu_cycles_low(void);
bool kernel_interrupt_dispatch(void);
void kernel_enable_interrupts(void);
void kernel_disable_interrupts(void);

bool kernel_platform_block_present(void);
bool kernel_platform_block_state(KernelPlatformBlockState *state);
uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer);
bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion);
void kernel_platform_block_ack_state(void);
bool kernel_input_pop(KernelInputEvent *event);

#endif
