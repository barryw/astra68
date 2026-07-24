#ifndef ASTRA_KERNEL_PLATFORM_H
#define ASTRA_KERNEL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_PLATFORM_QUANTUM_MS 5u
#define KERNEL_PLATFORM_QUANTUM_HZ \
    (1000u / KERNEL_PLATFORM_QUANTUM_MS)
#define KERNEL_PLATFORM_CPU_HZ 12500000u
#define KERNEL_PLATFORM_NS_PER_CPU_CYCLE 80u

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

typedef struct KernelPlatformCycleCount {
    uint32_t high;
    uint32_t low;
} KernelPlatformCycleCount;

void kernel_platform_interrupt_init(uint32_t cpu_hz);
uint32_t kernel_platform_quantum_cycles(void);
void kernel_platform_timer_arm(uint32_t cycles);
void kernel_platform_timer_disarm(void);
uint32_t kernel_platform_ticks(void);
uint32_t kernel_platform_cpu_cycles_low(void);
void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles);
uint64_t kernel_platform_monotonic_ns(void);
uint64_t kernel_platform_cycles_to_ns(uint64_t cycles);
bool kernel_platform_deadline_to_cycles(int64_t deadline_ns,
                                        uint64_t *deadline_cycles);
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

#if defined(KERNEL_PLATFORM_HOST_TEST)
#include "vesta.h"
VestaRegs *kernel_platform_test_registers(void);
#endif

#endif
