#include <astra/boot_progress.h>
#include "boot_progress.h"
#include "platform.h"

#include <astra/syscall.h>
#include <vega.h>
#include <vesta.h>

#include <assert.h>
#include <stdio.h>

#define PACK astra_boot_progress_pack
#define BOOTING ASTRA_BOOT_PROGRESS_STATE_BOOTING
#define DONE ASTRA_BOOT_PROGRESS_STATE_DONE
#define FAILED ASTRA_BOOT_PROGRESS_STATE_FAILED
#define FIXED ASTRA_BOOT_PROGRESS_FIXED_STEPS
#define FILL astra_boot_progress_fill

static const uint32_t boot_stages[] = {
    ASTRA_SUPERVISOR_STAGE_SELF_VERIFIED,
    ASTRA_SUPERVISOR_STAGE_VOLUME_FOUND,
    ASTRA_SUPERVISOR_STAGE_VOLUME_MOUNTED,
    ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED
};
_Static_assert(sizeof(boot_stages) / sizeof(boot_stages[0]) ==
                   ASTRA_BOOT_PROGRESS_SUPERVISOR_STEPS,
               "supervisor progress count");

static void test_transitions(void)
{
    uint32_t value = 0u;
    uint32_t next;

    /* The fields sit where the ABI says. */
    assert(FIXED == 18u);
    assert(PACK(FAILED, 1u, 0x15u, 0x3ffu, 0x155u) == 0x2aaffd55u);
    assert(astra_boot_progress_state(0x2aaffd55u) == FAILED);
    assert(astra_boot_progress_announced(0x2aaffd55u) == 1u);
    assert(astra_boot_progress_fixed(0x2aaffd55u) == 0x15u);
    assert(astra_boot_progress_count(0x2aaffd55u) == 0x3ffu);
    assert(astra_boot_progress_done(0x2aaffd55u) == 0x155u);

    /* Every fixed step increases the record. */
    for (uint32_t step = 0u; step < FIXED; ++step) {
        next = astra_boot_progress_advance(value);
        assert(next > value);
        value = next;
    }
    assert(value == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* One more fixed step is a miscount. */
    assert(astra_boot_progress_advance(value) ==
           PACK(FAILED, 0u, FIXED, 0u, 0u));

    /* The announcement. */
    next = astra_boot_progress_announce(value, 14u);
    assert(next == PACK(BOOTING, 1u, FIXED, 14u, 0u) && next > value);
    value = next;
    /* Once only, and no fixed step after it. */
    assert(astra_boot_progress_announce(value, 3u) ==
           PACK(FAILED, 1u, FIXED, 14u, 0u));
    assert(astra_boot_progress_advance(value) ==
           PACK(FAILED, 1u, FIXED, 14u, 0u));
    /* Announced before the fixed phase is complete: counted, but FAILED. */
    assert(astra_boot_progress_announce(PACK(BOOTING, 0u, FIXED - 1u, 0u, 0u), 3u) ==
           PACK(FAILED, 1u, FIXED - 1u, 3u, 0u));
    assert(astra_boot_progress_state(astra_boot_progress_announce(
               PACK(BOOTING, 0u, FIXED, 0u, 0u),
               ASTRA_BOOT_PROGRESS_SERVICES_MAX + 1u)) == FAILED);

    /* Fourteen services, then stage 8. */
    for (uint32_t service = 0u; service < 14u; ++service) {
        next = astra_boot_progress_service(value, 0u);
        assert(next > value);
        value = next;
    }
    assert(value == PACK(BOOTING, 1u, FIXED, 14u, 14u));
    /* A fifteenth report is a miscount. */
    assert(astra_boot_progress_service(value, 0u) ==
           PACK(FAILED, 1u, FIXED, 14u, 14u));
    next = astra_boot_progress_finish(value);
    assert(next == PACK(DONE, 1u, FIXED, 14u, 14u) && next > value);
    value = next;
    /* DONE takes no more steps; only a failure moves it, upward. */
    assert(astra_boot_progress_advance(value) == value);
    assert(astra_boot_progress_service(value, 0u) == value);
    assert(astra_boot_progress_fail(value) ==
           PACK(FAILED, 1u, FIXED, 14u, 14u));
    assert(astra_boot_progress_fail(value) > value);

    /* Stage 8 is DONE only when every step arrived. */
    assert(astra_boot_progress_finish(PACK(BOOTING, 1u, FIXED, 14u, 13u)) ==
           PACK(FAILED, 1u, FIXED, 14u, 13u));
    assert(astra_boot_progress_finish(PACK(BOOTING, 0u, FIXED, 0u, 0u)) ==
           PACK(FAILED, 0u, FIXED, 0u, 0u));
    /* No services at all is a complete boot. */
    assert(astra_boot_progress_finish(astra_boot_progress_announce(
               PACK(BOOTING, 0u, FIXED, 0u, 0u), 0u)) ==
           PACK(DONE, 1u, FIXED, 0u, 0u));

    /* A service that fails: FAILED, and the count still completes. */
    value = astra_boot_progress_service(PACK(BOOTING, 1u, FIXED, 2u, 0u), 1u);
    assert(value == PACK(FAILED, 1u, FIXED, 2u, 1u));
    value = astra_boot_progress_service(value, 0u);
    assert(value == PACK(FAILED, 1u, FIXED, 2u, 2u));
    assert(astra_boot_progress_finish(value) == value);
}

static void test_fill(void)
{
    /* 30% over fixed steps, then 70% over N services. */
    assert(FILL(0u, 880u) == 0u);
    assert(FILL(PACK(BOOTING, 0u, 7u, 0u, 0u), 880u) == 102u);
    assert(FILL(PACK(BOOTING, 0u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 7u), 880u) == 572u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 14u), 880u) == 880u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(DONE, 1u, FIXED, 0u, 0u), 880u) == 880u);
    assert(FILL(PACK(DONE, 1u, FIXED, 14u, 14u), 880u) == 880u);
}

/* platform.c's interrupt hooks; nothing here masks (as test_platform.c). */
uint16_t kernel_interrupt_save_disable(void)
{
    return 0x2000u;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    (void)status_register;
}

static uint32_t record(void)
{
    uint32_t value = 0u;

    assert(kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                            &value));
    return value;
}

static void rom_wrote(uint32_t value)
{
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                             value));
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                             ASTRA_BOOT_PROGRESS_HEADER));
}

static uint32_t command(uint32_t operation, uint32_t argument)
{
    return kernel_boot_progress_command(operation | argument);
}

/* What a boot does between handoff and the announcement. */
static void kernel_and_stages(void)
{
    for (uint32_t step = 0u; step < ASTRA_BOOT_PROGRESS_KERNEL_STEPS; ++step)
        kernel_boot_progress_step();
    for (size_t index = 0u; index < sizeof(boot_stages) / sizeof(boot_stages[0]);
         ++index)
        kernel_boot_progress_stage(boot_stages[index]);
}

static void test_kernel(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    VegaRegs *video = kernel_platform_test_vega_registers();
    const uint32_t rom = PACK(BOOTING, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS, 0u,
                              0u);
    uint32_t last;

    registers->SYS_STATUS = SYS_VIDEO_READY;
    video->ID = VEGA_ID_MAGIC;
    video->CAPS = VEGA_CAP_POST_TEXT;

    /* No ROM record: the kernel adopts nothing and writes nothing. */
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                             0u));
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                             0u));
    kernel_boot_progress_init();
    kernel_boot_progress_step();
    assert(record() == 0u);

    /* The ROM's record is adopted and continued, never decreasing. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    assert(record() == rom);
    last = record();
    kernel_boot_progress_step();
    assert(record() > last);
    for (uint32_t step = 1u; step < ASTRA_BOOT_PROGRESS_KERNEL_STEPS; ++step)
        kernel_boot_progress_step();
    for (size_t index = 0u; index < sizeof(boot_stages) / sizeof(boot_stages[0]);
         ++index) {
        last = record();
        kernel_boot_progress_stage(boot_stages[index]);
        assert(record() > last);
    }
    assert(record() == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* Past the last supervisor stage is not a stage: nothing moves. */
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_MAX + 1u);
    kernel_boot_progress_stage(ASTRA_PROGRESS_BOOT_SERVICES | 14u);
    assert(record() == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* A service report before the announcement is refused, unchanged. */
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(record() == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 3u) == ASTRA_SYSCALL_OK);
    assert(record() == PACK(BOOTING, 1u, FIXED, 3u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 3u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, 7u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(command(ASTRA_PROGRESS_BOOT_COMMAND_PREFIX | 0x3000u, 0u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(record() == PACK(BOOTING, 1u, FIXED, 3u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE,
                   ASTRA_PROGRESS_SERVICE_SKIPPED) == ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_TERMINAL);
    assert(record() == PACK(DONE, 1u, FIXED, 3u, 3u));
    assert(kernel_boot_progress_value() == record());
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    /* The console failing after boot is still a failure. */
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
    assert(record() == PACK(FAILED, 1u, FIXED, 3u, 3u));

    /* A ROM that miscounted is caught at handoff. */
    rom_wrote(PACK(BOOTING, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS - 1u, 0u, 0u));
    kernel_boot_progress_init();
    assert(record() == PACK(FAILED, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS - 1u, 0u,
                            0u));

    /* A service that fails READY: FAILED, and the count still completes. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    kernel_and_stages();
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 2u) == ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE,
                   ASTRA_PROGRESS_SERVICE_FAILED) == ASTRA_SYSCALL_OK);
    assert(astra_boot_progress_state(record()) == FAILED);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_TERMINAL);
    assert(record() == PACK(FAILED, 1u, FIXED, 2u, 2u));

    /* A panic or degraded exit marks FAILED from any state. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    kernel_boot_progress_fail();
    assert(record() == astra_boot_progress_fail(rom));

    /* Without the text page the kernel tracks but never writes. */
    rom_wrote(rom);
    registers->SYS_STATUS = 0u;
    kernel_boot_progress_init();
    kernel_boot_progress_step();
    assert(kernel_boot_progress_value() == PACK(BOOTING, 0u, 1u, 0u, 0u));
    registers->SYS_STATUS = SYS_VIDEO_READY;
    assert(record() == rom);
}

int main(void)
{
    test_transitions();
    test_fill();
    test_kernel();
    puts("KERNEL BOOT PROGRESS PASS");
    return 0;
}
