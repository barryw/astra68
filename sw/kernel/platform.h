#ifndef ASTRA_KERNEL_PLATFORM_H
#define ASTRA_KERNEL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct KernelBlockCompletion {
    uint32_t id;
    uint16_t status;
    uint16_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} KernelBlockCompletion;

typedef struct KernelInputEvent {
    uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
    uint32_t host_generation;
} KernelInputEvent;

void kernel_platform_interrupt_init(uint32_t cpu_hz);
uint32_t kernel_platform_ticks(void);
void kernel_interrupt_dispatch(void);
void kernel_enable_interrupts(void);

bool kernel_block_present(void);
uint32_t kernel_block_submit(uint32_t id, uint8_t operation,
                             uint8_t flags, uint64_t lba,
                             uint16_t sectors, uint32_t physical_buffer);
bool kernel_block_pop_completion(KernelBlockCompletion *completion);
void kernel_block_ack_state(void);
bool kernel_input_pop(KernelInputEvent *event);

#endif
