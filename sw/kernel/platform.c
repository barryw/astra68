#include "platform.h"

#include "vesta.h"

#define KERNEL_TIMER_VECTOR 80u
#define KERNEL_TIMER_IPL 4u
#define KERNEL_TICK_HZ 100u

static volatile uint32_t tick_count;

void kernel_platform_interrupt_init(uint32_t cpu_hz)
{
    uint32_t period = cpu_hz / KERNEL_TICK_HZ;
    if (period == 0u)
        period = 1u;

    tick_count = 0u;
    VESTA->IRQ_ENABLE = 0u;
    VESTA->TIMER[0].CTRL = 0u;
    VESTA->TIMER[0].STATUS = TMR_EXPIRED;
    VESTA->IRQ_ACK = IRQ_BIT(IRQ_SRC_TIMER0);
    VESTA->IRQ_CFG[IRQ_SRC_TIMER0] =
        IRQ_CFG_LEVEL(KERNEL_TIMER_IPL) |
        IRQ_CFG_VECTOR(KERNEL_TIMER_VECTOR);
    VESTA->TIMER[0].LOAD = period;
    VESTA->IRQ_ENABLE = IRQ_BIT(IRQ_SRC_TIMER0);
    VESTA->TIMER[0].CTRL = TMR_ENABLE | TMR_PERIODIC | TMR_IRQ_EN;
}

uint32_t kernel_platform_ticks(void)
{
    return tick_count;
}

void kernel_interrupt_dispatch(void)
{
    uint32_t current = VESTA->IRQ_CURRENT;
    uint32_t source = (current >> 8) & 0x1fu;
    uint32_t vector = (current >> 16) & 0xffu;

    if ((current & 0x80000000u) != 0u &&
        source == IRQ_SRC_TIMER0 && vector == KERNEL_TIMER_VECTOR) {
        VESTA->TIMER[0].STATUS = TMR_EXPIRED;
        ++tick_count;
        return;
    }

    // Quarantine an unexpected source before returning. The bootstrap kernel
    // enables only TIMER0; this prevents an accidental level interrupt from
    // trapping forever before the full device dispatcher is installed.
    if ((current & 0x80000000u) != 0u) {
        VESTA->IRQ_ENABLE &= ~IRQ_BIT(source);
        VESTA->IRQ_ACK = IRQ_BIT(source);
    }
}

bool kernel_block_present(void)
{
    return VESTA->BLOCK_ID == BLOCK_ID_MAGIC &&
           (VESTA->BLOCK_VERSION >> 16) ==
               (BLOCK_VERSION_1_0 >> 16);
}

uint32_t kernel_block_submit(uint32_t id, uint8_t operation,
                             uint8_t flags, uint64_t lba,
                             uint16_t sectors, uint32_t physical_buffer)
{
    if (!kernel_block_present())
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

bool kernel_block_pop_completion(KernelBlockCompletion *completion)
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

void kernel_block_ack_state(void)
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
