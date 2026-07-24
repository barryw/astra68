#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void clear_registers(VestaRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static void test_one_shot_configuration_and_restart(void)
{
    VestaRegs *registers = kernel_platform_test_registers();

    clear_registers(registers);
    kernel_platform_interrupt_init(12500000u);
    assert(kernel_platform_quantum_cycles() == 62500u);
    assert(registers->TIMER[0].LOAD == 62500u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
    assert((registers->TIMER[0].CTRL & TMR_PERIODIC) == 0u);
    assert(registers->IRQ_ENABLE == IRQ_BIT(IRQ_SRC_TIMER0));
    assert(registers->IRQ_CFG[IRQ_SRC_TIMER0] ==
           (IRQ_CFG_LEVEL(4u) | IRQ_CFG_VECTOR(80u)));

    kernel_platform_timer_arm(0u);
    assert(registers->TIMER[0].LOAD == 1u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
    kernel_platform_timer_disarm();
    assert(registers->TIMER[0].CTRL == 0u);

    registers->IRQ_CURRENT = 0x80000000u | (80u << 16) |
                             (IRQ_SRC_TIMER0 << 8) | 4u;
    assert(kernel_interrupt_dispatch());
    assert(kernel_platform_ticks() == 1u);
    assert(registers->TIMER[0].STATUS == TMR_EXPIRED);
    assert(registers->TIMER[0].LOAD == 62500u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
}

static void test_cycle_snapshot_and_unexpected_irq_quarantine(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelPlatformCycleCount cycles;

    clear_registers(registers);
    registers->CPU_CYCLES_LO = 0x89abcdefu;
    registers->CPU_CYCLES_HI = 0x01234567u;
    kernel_platform_cpu_cycles(&cycles);
    assert(cycles.low == 0x89abcdefu);
    assert(cycles.high == 0x01234567u);

    registers->IRQ_ENABLE = IRQ_BIT(7u) | IRQ_BIT(IRQ_SRC_TIMER0);
    registers->IRQ_CURRENT = 0x80000000u | (99u << 16) | (7u << 8) | 3u;
    assert(!kernel_interrupt_dispatch());
    assert((registers->IRQ_ENABLE & IRQ_BIT(7u)) == 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_TIMER0)) != 0u);
    assert(registers->IRQ_ACK == IRQ_BIT(7u));
}

int main(void)
{
    test_one_shot_configuration_and_restart();
    test_cycle_snapshot_and_unexpected_irq_quarantine();
    puts("platform tests passed");
    return 0;
}
