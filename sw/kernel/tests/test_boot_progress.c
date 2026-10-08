#include <astra/boot_progress.h>

#include <assert.h>
#include <stdio.h>

#define PACK astra_boot_progress_pack
#define BOOTING ASTRA_BOOT_PROGRESS_STATE_BOOTING
#define DONE ASTRA_BOOT_PROGRESS_STATE_DONE
#define FAILED ASTRA_BOOT_PROGRESS_STATE_FAILED
#define FIXED ASTRA_BOOT_PROGRESS_FIXED_STEPS
#define FILL astra_boot_progress_fill

static void test_transitions(void)
{
    uint32_t value = 0u;
    uint32_t next;

    /* The fields sit where the ABI says. */
    assert(FIXED == 21u);
    assert(PACK(FAILED, 1u, 0x15u, 0x3ffu, 0x155u) == 0x2aaffd55u);
    assert(astra_boot_progress_state(0x2aaffd55u) == FAILED);
    assert(astra_boot_progress_announced(0x2aaffd55u) == 1u);
    assert(astra_boot_progress_fixed(0x2aaffd55u) == 0x15u);
    assert(astra_boot_progress_count(0x2aaffd55u) == 0x3ffu);
    assert(astra_boot_progress_done(0x2aaffd55u) == 0x155u);

    /* The fixed phase: 21 steps, each one larger. */
    for (uint32_t step = 0u; step < FIXED; ++step) {
        next = astra_boot_progress_advance(value);
        assert(next > value);
        value = next;
    }
    assert(value == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* A 22nd fixed step is a miscount. */
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
    assert(astra_boot_progress_announce(PACK(BOOTING, 0u, 20u, 0u, 0u), 3u) ==
           PACK(FAILED, 1u, 20u, 3u, 0u));
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
    /* 30% over 21 equal fixed steps, then 70% over N services. */
    assert(FILL(0u, 880u) == 0u);
    assert(FILL(PACK(BOOTING, 0u, 7u, 0u, 0u), 880u) == 88u);
    assert(FILL(PACK(BOOTING, 0u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 7u), 880u) == 572u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 14u), 880u) == 880u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(DONE, 1u, FIXED, 0u, 0u), 880u) == 880u);
    assert(FILL(PACK(DONE, 1u, FIXED, 14u, 14u), 880u) == 880u);
}

int main(void)
{
    test_transitions();
    test_fill();
    puts("KERNEL BOOT PROGRESS PASS");
    return 0;
}
