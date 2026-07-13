#include "../pmmu030.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE UINT32_C(0x10000)
#define NO_FAILURE UINT32_MAX

typedef struct memory_bus {
    uint8_t data[MEMORY_SIZE];
    uint32_t fail_read;
    uint32_t fail_write;
    unsigned reads;
    unsigned writes;
    unsigned walk_begins;
    unsigned walk_ends;
    unsigned walk_depth;
} memory_bus;

static unsigned tests_run;

static void fail(const char *test, int line, const char *expression)
{
    fprintf(stderr, "FAIL %s:%d: %s\n", test, line, expression);
    exit(1);
}

#define CHECK(test_name, expression) \
    do { if (!(expression)) fail((test_name), __LINE__, #expression); } while (0)

static void memory_init(memory_bus *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->fail_read = NO_FAILURE;
    memory->fail_write = NO_FAILURE;
}

static void put32(memory_bus *memory, uint32_t address, uint32_t value)
{
    if (address > MEMORY_SIZE - 4u) {
        abort();
    }
    memory->data[address] = (uint8_t)(value >> 24);
    memory->data[address + 1u] = (uint8_t)(value >> 16);
    memory->data[address + 2u] = (uint8_t)(value >> 8);
    memory->data[address + 3u] = (uint8_t)value;
}

static uint32_t get32(const memory_bus *memory, uint32_t address)
{
    if (address > MEMORY_SIZE - 4u) {
        abort();
    }
    return ((uint32_t)memory->data[address] << 24) |
           ((uint32_t)memory->data[address + 1u] << 16) |
           ((uint32_t)memory->data[address + 2u] << 8) |
           memory->data[address + 3u];
}

static bool read32(void *context, uint32_t address, uint32_t *value)
{
    memory_bus *memory = context;
    ++memory->reads;
    if (address == memory->fail_read || address > MEMORY_SIZE - 4u) {
        return false;
    }
    *value = get32(memory, address);
    return true;
}

static bool write32(void *context, uint32_t address, uint32_t value)
{
    memory_bus *memory = context;
    ++memory->writes;
    if (address == memory->fail_write || address > MEMORY_SIZE - 4u) {
        return false;
    }
    put32(memory, address, value);
    return true;
}

static void begin_walk(void *context)
{
    memory_bus *memory = context;
    ++memory->walk_begins;
    ++memory->walk_depth;
}

static void end_walk(void *context)
{
    memory_bus *memory = context;
    ++memory->walk_ends;
    if (memory->walk_depth == 0u) {
        abort();
    }
    --memory->walk_depth;
}

static pmmu030_bus make_bus(memory_bus *memory)
{
    pmmu030_bus bus;
    bus.context = memory;
    bus.read32 = read32;
    bus.write32 = write32;
    bus.begin_walk = begin_walk;
    bus.end_walk = end_walk;
    return bus;
}

static uint32_t tc_two_level(unsigned is, unsigned a, unsigned b,
                             unsigned ps, uint32_t flags)
{
    return PMMU030_TC_ENABLE | flags | ((uint32_t)ps << 20) |
           ((uint32_t)is << 16) | ((uint32_t)a << 12) |
           ((uint32_t)b << 8);
}

static unsigned index_for(uint32_t logical, unsigned initial_shift,
                          unsigned width)
{
    return (logical >> (32u - initial_shift - width)) &
           ((1u << width) - 1u);
}

static void configure_short_two_level(pmmu030_state *state,
                                      memory_bus *memory, uint32_t logical,
                                      uint32_t page_descriptor)
{
    unsigned a = index_for(logical, 0u, 10u);
    unsigned b = index_for(logical, 10u, 10u);

    CHECK("configure", pmmu030_write_crp(state, UINT32_C(0x7fff0002),
                                          UINT32_C(0x1000), true));
    CHECK("configure", pmmu030_write_tc(state,
                                         tc_two_level(0u, 10u, 10u, 12u, 0u),
                                         true));
    put32(memory, UINT32_C(0x1000) + a * 4u, UINT32_C(0x2002));
    put32(memory, UINT32_C(0x2000) + b * 4u, page_descriptor);
}

static void test_registers_and_reset(void)
{
    const char *name = "registers_and_reset";
    pmmu030_state state;

    pmmu030_init(&state);
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(0u, 10u, 10u, 12u, 0u), true));
    CHECK(name, (state.tc & PMMU030_TC_ENABLE) != 0u);
    CHECK(name, pmmu030_page_shift(&state) == 12u);
    CHECK(name, !pmmu030_write_tc(&state, UINT32_C(0xffffffff), true));
    CHECK(name, state.tc == UINT32_C(0x03ffffff));

    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0xffffffff),
                                    UINT32_C(0xffffffff), true));
    CHECK(name, state.crp[0] == UINT32_C(0xffff0003));
    CHECK(name, state.crp[1] == UINT32_C(0xfffffff0));
    CHECK(name, !pmmu030_write_crp(&state, UINT32_C(0xfffffffc),
                                    UINT32_C(0xffffffff), true));

    pmmu030_write_tt0(&state, UINT32_C(0xffffffff), true);
    pmmu030_write_tt1(&state, UINT32_C(0x12348777), true);
    CHECK(name, state.tt0 == PMMU030_TT_MASK);
    CHECK(name, state.tt1 == UINT32_C(0x12348777));
    pmmu030_write_mmusr(&state, UINT16_C(0xffff));
    CHECK(name, state.mmusr == PMMU030_MMUSR_MASK);

    state.atc[0].valid = true;
    state.tc |= PMMU030_TC_ENABLE;
    pmmu030_reset(&state);
    CHECK(name, (state.tc & PMMU030_TC_ENABLE) == 0u);
    CHECK(name, (state.tt0 & PMMU030_TT_ENABLE) == 0u);
    CHECK(name, (state.tt1 & PMMU030_TT_ENABLE) == 0u);
    CHECK(name, state.atc[0].valid);
    ++tests_run;
}

static void test_transparent_translation(void)
{
    const char *name = "transparent_translation";
    pmmu030_state state;
    pmmu030_result result;
    uint32_t tt;

    pmmu030_init(&state);
    tt = UINT32_C(0x12000000) | PMMU030_TT_ENABLE | UINT32_C(0x400) |
         UINT32_C(0x100) | UINT32_C(0x10);
    pmmu030_write_tt0(&state, tt, true);
    result = pmmu030_translate(&state, NULL, UINT32_C(0x12345678), 1u,
                               PMMU030_ACCESS_WRITE);
    CHECK(name, result.transparent);
    CHECK(name, result.cache_inhibit);
    CHECK(name, result.physical_address == UINT32_C(0x12345678));

    result = pmmu030_translate(&state, NULL, UINT32_C(0x13345678), 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, !result.transparent);

    pmmu030_write_tt0(&state, UINT32_C(0x12008210), true);
    result = pmmu030_translate(&state, NULL, UINT32_C(0x12345678), 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.transparent);
    result = pmmu030_translate(&state, NULL, UINT32_C(0x12345678), 1u,
                               PMMU030_ACCESS_WRITE);
    CHECK(name, !result.transparent);
    result = pmmu030_translate(&state, NULL, UINT32_C(0x12345678), 1u,
                               PMMU030_ACCESS_RMW);
    CHECK(name, !result.transparent);

    result = pmmu030_ptest(&state, NULL, UINT32_C(0x12345678), 1u,
                           PMMU030_ACCESS_READ, 0u);
    CHECK(name, result.mmusr == PMMU030_MMUSR_T);
    ++tests_run;
}

static void test_short_walk_atc_and_history(void)
{
    const char *name = "short_walk_atc_and_history";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x12345abc);
    unsigned a = index_for(logical, 0u, 10u);
    unsigned b = index_for(logical, 10u, 10u);
    uint32_t table_address = UINT32_C(0x1000) + a * 4u;
    uint32_t page_address = UINT32_C(0x2000) + b * 4u;
    unsigned reads;

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    configure_short_two_level(&state, &memory, logical,
                              UINT32_C(0x90000041));

    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.translated);
    CHECK(name, result.physical_address == UINT32_C(0x90000abc));
    CHECK(name, result.cache_inhibit);
    CHECK(name, result.levels == 2u);
    CHECK(name, (get32(&memory, table_address) & 8u) != 0u);
    CHECK(name, (get32(&memory, page_address) & 8u) != 0u);
    CHECK(name, (get32(&memory, page_address) & UINT32_C(0x10)) == 0u);
    CHECK(name, memory.walk_begins == 1u && memory.walk_ends == 1u);
    CHECK(name, memory.walk_depth == 0u);

    reads = memory.reads;
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.atc_hit);
    CHECK(name, memory.reads == reads);

    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_WRITE);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.modified);
    CHECK(name, (get32(&memory, page_address) & UINT32_C(0x10)) != 0u);
    CHECK(name, memory.walk_begins == 2u && memory.walk_ends == 2u);
    ++tests_run;
}

static void test_write_protection(void)
{
    const char *name = "write_protection";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x42345abc);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    configure_short_two_level(&state, &memory, logical,
                              UINT32_C(0xa0000005));

    result = pmmu030_translate(&state, &bus, logical, 2u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.write_protect);
    result = pmmu030_translate(&state, &bus, logical, 2u,
                               PMMU030_ACCESS_WRITE);
    CHECK(name, result.atc_hit);
    CHECK(name, result.fault == PMMU030_FAULT_WRITE_PROTECT);
    ++tests_run;
}

static void test_limits_and_error_atc(void)
{
    const char *name = "limits_and_error_atc";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x00006000);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x00050002),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(15u, 5u, 0u, 12u, 0u), true));
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_LIMIT);
    CHECK(name, result.levels == 0u);
    CHECK(name, memory.reads == 0u);
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.atc_hit);
    CHECK(name, result.fault == PMMU030_FAULT_ATC_BUS);

    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_READ, 7u);
    CHECK(name, (result.mmusr & (PMMU030_MMUSR_L | PMMU030_MMUSR_I)) ==
                (PMMU030_MMUSR_L | PMMU030_MMUSR_I));
    CHECK(name, (result.mmusr & PMMU030_MMUSR_N) == 0u);
    ++tests_run;
}

static void test_supervisor_and_srp(void)
{
    const char *name = "supervisor_and_srp";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x00023044);
    unsigned index = index_for(logical, 12u, 8u);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x7fff0003),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_srp(&state, UINT32_C(0x7fff0003),
                                   UINT32_C(0x2000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(12u, 8u, 0u, 12u,
                                              PMMU030_TC_SRE), true));
    put32(&memory, UINT32_C(0x1000) + index * 8u,
          UINT32_C(0x7ffffd01));
    put32(&memory, UINT32_C(0x1004) + index * 8u,
          UINT32_C(0xb0000000));
    put32(&memory, UINT32_C(0x2000) + index * 8u,
          UINT32_C(0x7ffffc01));
    put32(&memory, UINT32_C(0x2004) + index * 8u,
          UINT32_C(0xc0000000));

    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_SUPERVISOR);
    CHECK(name, (get32(&memory, UINT32_C(0x1000) + index * 8u) & 8u) == 0u);
    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_READ, 7u);
    CHECK(name, (result.mmusr & (PMMU030_MMUSR_S | PMMU030_MMUSR_I)) ==
                (PMMU030_MMUSR_S | PMMU030_MMUSR_I));

    result = pmmu030_translate(&state, &bus, logical, 5u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.physical_address == UINT32_C(0xc0000044));
    ++tests_run;
}

static void test_early_termination(void)
{
    const char *name = "early_termination";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x12345678);
    unsigned a = index_for(logical, 0u, 8u);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x7fff0002),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(0u, 8u, 12u, 12u, 0u), true));
    put32(&memory, UINT32_C(0x1000) + a * 4u, UINT32_C(0x80000001));
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.levels == 1u);
    CHECK(name, result.physical_address == UINT32_C(0x80345678));
    ++tests_run;
}

static void test_indirect_descriptor(void)
{
    const char *name = "indirect_descriptor";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x000450aa);
    unsigned index = index_for(logical, 15u, 5u);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x7fff0002),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(15u, 5u, 0u, 12u, 0u), true));
    put32(&memory, UINT32_C(0x1000) + index * 4u, UINT32_C(0x3003));
    put32(&memory, UINT32_C(0x3000), UINT32_C(0x0000fc01));
    put32(&memory, UINT32_C(0x3004), UINT32_C(0xd0000000));
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.levels == 2u);
    CHECK(name, result.last_descriptor_address == UINT32_C(0x3000));
    /* IS bits are not used by the search and therefore remain in the offset. */
    CHECK(name, result.physical_address == UINT32_C(0xd00400aa));
    CHECK(name, (get32(&memory, UINT32_C(0x3000)) & 8u) != 0u);
    ++tests_run;
}

static void test_ptest_no_side_effects_and_levels(void)
{
    const char *name = "ptest_no_side_effects_and_levels";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x22345abc);
    unsigned a = index_for(logical, 0u, 10u);
    uint32_t table_address = UINT32_C(0x1000) + a * 4u;

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    configure_short_two_level(&state, &memory, logical,
                              UINT32_C(0xe0000011));
    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_WRITE, 1u);
    CHECK(name, result.levels == 1u);
    CHECK(name, result.last_descriptor_address == table_address);
    CHECK(name, result.mmusr == UINT16_C(1));
    CHECK(name, (get32(&memory, table_address) & 8u) == 0u);
    CHECK(name, !state.atc[0].valid);

    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_WRITE, 7u);
    CHECK(name, result.levels == 2u);
    CHECK(name, (result.mmusr & (PMMU030_MMUSR_M | PMMU030_MMUSR_N)) ==
                (PMMU030_MMUSR_M | UINT16_C(2)));
    CHECK(name, memory.writes == 0u);
    ++tests_run;
}

static void test_pload_with_translation_disabled(void)
{
    const char *name = "pload_with_translation_disabled";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x32345abc);
    unsigned b = index_for(logical, 10u, 10u);
    uint32_t page_address = UINT32_C(0x2000) + b * 4u;

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    configure_short_two_level(&state, &memory, logical,
                              UINT32_C(0xf0000001));
    state.tc &= ~PMMU030_TC_ENABLE;
    state.mmusr = UINT16_C(0x2222);
    result = pmmu030_pload(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_WRITE);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, state.mmusr == UINT16_C(0x2222));
    CHECK(name, (get32(&memory, page_address) & UINT32_C(0x18)) ==
                UINT32_C(0x18));
    CHECK(name, state.atc[0].valid);

    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, !result.atc_hit);
    CHECK(name, result.physical_address == logical);
    ++tests_run;
}

static void test_flush_selection(void)
{
    const char *name = "flush_selection";
    pmmu030_state state;
    uint32_t address = UINT32_C(0x12345000);

    pmmu030_init(&state);
    state.tc = UINT32_C(0x00c00000);
    state.atc[0].valid = true;
    state.atc[0].function_code = 1u;
    state.atc[0].logical_page = address;
    state.atc[1].valid = true;
    state.atc[1].function_code = 5u;
    state.atc[1].logical_page = address;
    state.atc[2].valid = true;
    state.atc[2].function_code = 1u;
    state.atc[2].logical_page = UINT32_C(0x99999000);

    pmmu030_flush(&state, 1u, 7u, &address);
    CHECK(name, !state.atc[0].valid);
    CHECK(name, state.atc[1].valid);
    CHECK(name, state.atc[2].valid);
    pmmu030_flush(&state, 1u, 7u, NULL);
    CHECK(name, !state.atc[2].valid);
    CHECK(name, state.atc[1].valid);
    ++tests_run;
}

static void test_table_bus_errors(void)
{
    const char *name = "table_bus_errors";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x62345abc);
    unsigned a = index_for(logical, 0u, 10u);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    configure_short_two_level(&state, &memory, logical,
                              UINT32_C(0x70000001));
    memory.fail_read = UINT32_C(0x1000) + a * 4u;
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_TABLE_BUS);
    CHECK(name, memory.walk_depth == 0u);
    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_READ, 7u);
    CHECK(name, (result.mmusr & (PMMU030_MMUSR_B | PMMU030_MMUSR_I)) ==
                (PMMU030_MMUSR_B | PMMU030_MMUSR_I));
    ++tests_run;
}

static void test_function_code_lookup(void)
{
    const char *name = "function_code_lookup";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x000050aa);
    unsigned index = index_for(logical, 15u, 5u);

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    /* Root upper limit zero is deliberately ignored for the FC level. */
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x00000002),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(15u, 5u, 0u, 12u,
                                              PMMU030_TC_FCL), true));
    put32(&memory, UINT32_C(0x1004), UINT32_C(0x2002)); /* FC 1 */
    put32(&memory, UINT32_C(0x2000) + index * 4u,
          UINT32_C(0x90000001));
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.levels == 2u);
    CHECK(name, result.physical_address == UINT32_C(0x900000aa));
    ++tests_run;
}

static void test_long_early_limit(void)
{
    const char *name = "long_early_limit";
    pmmu030_state state;
    memory_bus memory;
    pmmu030_bus bus;
    pmmu030_result result;
    uint32_t logical = UINT32_C(0x00023044); /* A=2, B=3 */
    unsigned a = index_for(logical, 12u, 4u);
    uint32_t descriptor_address = UINT32_C(0x1000) + a * 8u;

    pmmu030_init(&state);
    memory_init(&memory);
    bus = make_bus(&memory);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x7fff0003),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(12u, 4u, 4u, 12u, 0u), true));
    /* Long early page, upper limit 2 on the following B index. */
    put32(&memory, descriptor_address, UINT32_C(0x0002fc01));
    put32(&memory, descriptor_address + 4u, UINT32_C(0xa0000000));
    result = pmmu030_translate(&state, &bus, logical, 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_LIMIT);
    CHECK(name, result.levels == 1u);
    CHECK(name, (get32(&memory, descriptor_address) & 8u) != 0u);

    pmmu030_flush_all(&state);
    result = pmmu030_ptest(&state, &bus, logical, 1u,
                           PMMU030_ACCESS_READ, 7u);
    CHECK(name, (result.mmusr & (PMMU030_MMUSR_L | PMMU030_MMUSR_I |
                                 PMMU030_MMUSR_N)) ==
                (PMMU030_MMUSR_L | PMMU030_MMUSR_I | UINT16_C(1)));
    ++tests_run;
}

static void test_root_early_termination(void)
{
    const char *name = "root_early_termination";
    pmmu030_state state;
    pmmu030_result result;

    pmmu030_init(&state);
    CHECK(name, pmmu030_write_crp(&state, UINT32_C(0x00050001),
                                   UINT32_C(0x1000), true));
    CHECK(name, pmmu030_write_tc(&state,
                                 tc_two_level(15u, 5u, 0u, 12u, 0u), true));
    result = pmmu030_translate(&state, NULL, UINT32_C(0x00005044), 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_NONE);
    CHECK(name, result.levels == 0u);
    CHECK(name, result.physical_address == UINT32_C(0x00006044));

    pmmu030_flush_all(&state);
    result = pmmu030_translate(&state, NULL, UINT32_C(0x00006044), 1u,
                               PMMU030_ACCESS_READ);
    CHECK(name, result.fault == PMMU030_FAULT_LIMIT);
    ++tests_run;
}

int main(void)
{
    test_registers_and_reset();
    test_transparent_translation();
    test_short_walk_atc_and_history();
    test_write_protection();
    test_limits_and_error_atc();
    test_supervisor_and_srp();
    test_early_termination();
    test_indirect_descriptor();
    test_ptest_no_side_effects_and_levels();
    test_pload_with_translation_disabled();
    test_flush_selection();
    test_table_bus_errors();
    test_function_code_lookup();
    test_long_early_limit();
    test_root_early_termination();
    printf("PASS: %u MC68030 PMMU conformance groups\n", tests_run);
    return 0;
}
