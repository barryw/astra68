#include "boot_progress.h"

#include "platform.h"

#include <astra/boot_progress.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>

#include <stdbool.h>

/*
 * One writer at a time. Init and its steps run before the first user thread;
 * the stage hook, boot commands and the degraded exit (from a syscall or a
 * user fault) run in the kernel's trap path on the one CPU, so none overlaps
 * another. Only the panic can interrupt one, and it never returns.
 */
static bool boot_progress_enabled;
static uint32_t boot_progress;

static void publish(uint32_t value)
{
    boot_progress = value;
    if (boot_progress_enabled)
        (void)kernel_platform_post_text_write32(
            ASTRA_BOOT_PROGRESS_VALUE_OFFSET, value);
}

void kernel_boot_progress_init(void)
{
    uint32_t header = 0u;
    uint32_t value = 0u;

    boot_progress_enabled =
        kernel_platform_post_text_present() &&
        kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                         &header) &&
        header == ASTRA_BOOT_PROGRESS_HEADER &&
        kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                         &value);
    boot_progress = boot_progress_enabled ? value : 0u;
    /* The ROM's count is part of the exact total: a miscount is a fault. */
    if (boot_progress_enabled &&
        astra_boot_progress_fixed(value) != ASTRA_BOOT_PROGRESS_ROM_STEPS)
        publish(astra_boot_progress_fail(value));
}

uint32_t kernel_boot_progress_value(void)
{
    return boot_progress;
}

void kernel_boot_progress_step(void)
{
    publish(astra_boot_progress_advance(boot_progress));
}

void kernel_boot_progress_fail(void)
{
    publish(astra_boot_progress_fail(boot_progress));
}

void kernel_boot_progress_stage(uint32_t stage)
{
    if (stage > ASTRA_SUPERVISOR_STAGE_MAX)
        return;
    if (stage == ASTRA_SUPERVISOR_STAGE_TERMINAL)
        publish(astra_boot_progress_finish(boot_progress));
    else if (stage == ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED)
        kernel_boot_progress_fail();
    else
        kernel_boot_progress_step();
}

uint32_t kernel_boot_progress_command(uint32_t command)
{
    uint32_t argument = command & ASTRA_PROGRESS_BOOT_ARGUMENT_MASK;
    uint32_t announced = astra_boot_progress_announced(boot_progress);

    if (astra_boot_progress_state(boot_progress) ==
        ASTRA_BOOT_PROGRESS_STATE_DONE)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    switch (command & ~ASTRA_PROGRESS_BOOT_ARGUMENT_MASK) {
    case ASTRA_PROGRESS_BOOT_SERVICES:
        if (announced)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        publish(astra_boot_progress_announce(boot_progress, argument));
        return ASTRA_SYSCALL_OK;
    case ASTRA_PROGRESS_BOOT_SERVICE:
        if (!announced || argument > ASTRA_PROGRESS_SERVICE_SKIPPED)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        publish(astra_boot_progress_service(
            boot_progress, argument == ASTRA_PROGRESS_SERVICE_FAILED));
        return ASTRA_SYSCALL_OK;
    default:
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
}
