#include "platform.h"

#include "vesta.h"

#if defined(KERNEL_PLATFORM_HOST_TEST)
static VestaRegs platform_test_registers;
#undef VESTA
#define VESTA (&platform_test_registers)

VestaRegs *kernel_platform_test_registers(void)
{
    return (VestaRegs *)&platform_test_registers;
}
#endif

#define KERNEL_TIMER_VECTOR 80u
#define KERNEL_TIMER_IPL 4u

static volatile uint32_t tick_count;
static uint32_t quantum_cycles;

static uint32_t divide_ns_limb(uint32_t remainder, uint32_t limb,
                               uint32_t *next_remainder)
{
    uint32_t dividend = (remainder << 16) | (limb & 0xffffu);
    uint32_t quotient = dividend / KERNEL_PLATFORM_NS_PER_CPU_CYCLE;

    *next_remainder = dividend -
        quotient * KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    return quotient;
}

static uint64_t ceil_nanoseconds_to_cycles(uint64_t nanoseconds)
{
    uint32_t high = (uint32_t)(nanoseconds >> 32);
    uint32_t low = (uint32_t)nanoseconds;
    uint32_t quotient_high;
    uint32_t quotient_low;
    uint32_t remainder = 0u;

    quotient_high = divide_ns_limb(remainder, high >> 16, &remainder) << 16;
    quotient_high |= divide_ns_limb(remainder, high, &remainder);
    quotient_low = divide_ns_limb(remainder, low >> 16, &remainder) << 16;
    quotient_low |= divide_ns_limb(remainder, low, &remainder);
    if (remainder != 0u && ++quotient_low == 0u)
        ++quotient_high;
    return ((uint64_t)quotient_high << 32) | quotient_low;
}

uint32_t kernel_platform_quantum_cycles(void)
{
    return quantum_cycles;
}

void kernel_platform_timer_arm(uint32_t cycles)
{
    if (cycles == 0u)
        cycles = 1u;
    VESTA->TIMER[0].LOAD = cycles;
    VESTA->TIMER[0].CTRL = TMR_ENABLE | TMR_IRQ_EN;
}

void kernel_platform_timer_disarm(void)
{
    VESTA->TIMER[0].CTRL = 0u;
}

void kernel_platform_interrupt_init(uint32_t cpu_hz)
{
    quantum_cycles = cpu_hz / KERNEL_PLATFORM_QUANTUM_HZ;
    if (quantum_cycles == 0u)
        quantum_cycles = 1u;

    tick_count = 0u;
    VESTA->IRQ_ENABLE = 0u;
    kernel_platform_timer_disarm();
    VESTA->TIMER[0].STATUS = TMR_EXPIRED;
    VESTA->IRQ_ACK = IRQ_BIT(IRQ_SRC_TIMER0);
    VESTA->IRQ_CFG[IRQ_SRC_TIMER0] =
        IRQ_CFG_LEVEL(KERNEL_TIMER_IPL) |
        IRQ_CFG_VECTOR(KERNEL_TIMER_VECTOR);
    VESTA->IRQ_ENABLE = IRQ_BIT(IRQ_SRC_TIMER0);
    kernel_platform_timer_arm(quantum_cycles);
}

uint32_t kernel_platform_ticks(void)
{
    return tick_count;
}

uint32_t kernel_platform_cpu_cycles_low(void)
{
    return VESTA->CPU_CYCLES_LO;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    // Reading LO latches the coherent HI value for the following MMIO read.
    uint32_t low = VESTA->CPU_CYCLES_LO;
    uint32_t high = VESTA->CPU_CYCLES_HI;

    cycles->high = high;
    cycles->low = low;
}

uint64_t kernel_platform_cycles_to_ns(uint64_t cycles)
{
    const uint64_t maximum = (uint64_t)INT64_MAX - 1u;

    if (cycles > maximum / KERNEL_PLATFORM_NS_PER_CPU_CYCLE)
        return maximum;
    return cycles * KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
}

uint64_t kernel_platform_monotonic_ns(void)
{
    KernelPlatformCycleCount snapshot;
    uint64_t cycles;

    kernel_platform_cpu_cycles(&snapshot);
    cycles = ((uint64_t)snapshot.high << 32) | snapshot.low;
    return kernel_platform_cycles_to_ns(cycles);
}

bool kernel_platform_deadline_to_cycles(int64_t deadline_ns,
                                        uint64_t *deadline_cycles)
{
    if (deadline_cycles == NULL || deadline_ns < 0)
        return false;
    if (deadline_ns == INT64_MAX) {
        *deadline_cycles = UINT64_MAX;
        return true;
    }
    *deadline_cycles = ceil_nanoseconds_to_cycles((uint64_t)deadline_ns);
    return true;
}

bool kernel_interrupt_dispatch(void)
{
    uint32_t current = VESTA->IRQ_CURRENT;
    uint32_t source = (current >> 8) & 0x1fu;
    uint32_t vector = (current >> 16) & 0xffu;

    if ((current & 0x80000000u) != 0u &&
        source == IRQ_SRC_TIMER0 && vector == KERNEL_TIMER_VECTOR) {
        VESTA->TIMER[0].STATUS = TMR_EXPIRED;
        ++tick_count;
        kernel_platform_timer_arm(quantum_cycles);
        return true;
    }

    // Quarantine an unexpected source before returning. The bootstrap kernel
    // enables only TIMER0; this prevents an accidental level interrupt from
    // trapping forever before the full device dispatcher is installed.
    if ((current & 0x80000000u) != 0u) {
        VESTA->IRQ_ENABLE &= ~IRQ_BIT(source);
        VESTA->IRQ_ACK = IRQ_BIT(source);
    }
    return false;
}

bool kernel_platform_block_present(void)
{
    return VESTA->BLOCK_ID == BLOCK_ID_MAGIC &&
           (VESTA->BLOCK_VERSION >> 16) ==
               (BLOCK_VERSION_1_0 >> 16);
}

bool kernel_platform_block_state(KernelPlatformBlockState *state)
{
    uint32_t capabilities_before;
    uint32_t flags_before;
    uint32_t media_before;
    uint32_t host_before;
    uint32_t size_hi_before;
    uint32_t size_lo_before;
    uint32_t max_before;
    uint32_t capabilities_after;
    uint32_t flags_after;
    uint32_t media_after;
    uint32_t host_after;
    uint32_t size_hi_after;
    uint32_t size_lo_after;
    uint32_t max_after;

    if (state == 0 || !kernel_platform_block_present())
        return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        capabilities_before = VESTA->BLOCK_CAPS;
        flags_before = VESTA->BLOCK_STATE;
        host_before = VESTA->BLOCK_HOST_GEN;
        media_before = VESTA->BLOCK_MEDIA_GEN;
        size_hi_before = VESTA->BLOCK_MEDIA_SIZE_HI;
        size_lo_before = VESTA->BLOCK_MEDIA_SIZE_LO;
        max_before = VESTA->BLOCK_MAX_SECTORS;

        capabilities_after = VESTA->BLOCK_CAPS;
        flags_after = VESTA->BLOCK_STATE;
        host_after = VESTA->BLOCK_HOST_GEN;
        media_after = VESTA->BLOCK_MEDIA_GEN;
        size_hi_after = VESTA->BLOCK_MEDIA_SIZE_HI;
        size_lo_after = VESTA->BLOCK_MEDIA_SIZE_LO;
        max_after = VESTA->BLOCK_MAX_SECTORS;
        if (capabilities_before == capabilities_after &&
            flags_before == flags_after && host_before == host_after &&
            media_before == media_after && size_hi_before == size_hi_after &&
            size_lo_before == size_lo_after && max_before == max_after) {
            state->capabilities = capabilities_after;
            state->state_flags = flags_after;
            state->media_generation = media_after;
            state->host_generation = host_after;
            state->media_sectors = ((uint64_t)size_hi_after << 32) |
                                   size_lo_after;
            state->max_sectors = (uint16_t)max_after;
            state->reserved = 0u;
            return true;
        }
    }
    return false;
}

uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer)
{
    if (!kernel_platform_block_present())
        return BLOCK_ERROR_NO_MEDIA;
    if ((VESTA->BLOCK_QUEUE & BLOCK_QUEUE_REQUEST_READY) == 0u)
        return BLOCK_ERROR_QUEUE_FULL;

    VESTA->BLOCK_ERROR = 0xffffffffu;
    VESTA->BLOCK_REQ_ID = id;
    VESTA->BLOCK_REQ_OP = ((uint32_t)flags << 8) | operation;
    VESTA->BLOCK_REQ_LBA_HI = (uint32_t)(lba >> 32);
    VESTA->BLOCK_REQ_LBA_LO = (uint32_t)lba;
    VESTA->BLOCK_REQ_SECTORS = sectors;
    VESTA->BLOCK_REQ_BUFFER = physical_buffer;
    VESTA->BLOCK_REQ_SUBMIT = BLOCK_SUBMIT;
    return VESTA->BLOCK_ERROR;
}

bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion)
{
    uint32_t queue;
    uint32_t status;
    if (completion == 0)
        return false;
    queue = VESTA->BLOCK_QUEUE;
    if ((queue & BLOCK_QUEUE_COMPLETION_VALID) == 0u)
        return false;

    completion->id = VESTA->BLOCK_CPL_ID;
    status = VESTA->BLOCK_CPL_STATUS;
    completion->status = (uint16_t)(status >> 16);
    completion->sectors = (uint16_t)status;
    completion->detail = VESTA->BLOCK_CPL_DETAIL;
    completion->media_generation = VESTA->BLOCK_CPL_MEDIA_GEN;
    completion->host_generation = VESTA->BLOCK_CPL_HOST_GEN;
    VESTA->BLOCK_CPL_POP = BLOCK_CPL_POP_BIT;
    return true;
}

void kernel_platform_block_ack_state(void)
{
    VESTA->BLOCK_STATE_ACK = BLOCK_STATE_ACK_BIT;
}

bool kernel_input_pop(KernelInputEvent *event)
{
    if (event == 0 || (VESTA->INPUT_STATUS & INPUT_EVENT_VALID) == 0u)
        return false;
    event->header = VESTA->INPUT_HEADER;
    event->value = VESTA->INPUT_VALUE;
    event->timestamp_ms = VESTA->INPUT_TIMESTAMP;
    event->device_sequence = VESTA->INPUT_DEVICE_SEQ;
    event->host_generation = VESTA->INPUT_HOST_GEN;
    VESTA->INPUT_POP = INPUT_POP_BIT;
    return true;
}
