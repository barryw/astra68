#include "m68k.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE 0x10000u
#define FORMAT_A_FRAME UINT32_C(0x00007fe0)
#define FORMAT_B_FRAME UINT32_C(0x00007fa4)

static uint8_t memory[MEMORY_SIZE];
static unsigned tests_run;
static uint32_t watched_write_address = UINT32_MAX;
static unsigned watched_write_count;

static void fail(const char *test, int line, const char *expression)
{
    fprintf(stderr, "FAIL %s:%d: %s\n", test, line, expression);
    exit(1);
}

#define CHECK(test_name, expression) \
    do { if (!(expression)) fail((test_name), __LINE__, #expression); } while (0)

static void check_address(uint32_t address, unsigned size)
{
    if (address >= MEMORY_SIZE || size > MEMORY_SIZE - address) {
        fprintf(stderr, "test memory access outside range: %08x + %u"
                " (pc=%08x sr=%04x)\n", address, size,
                m68k_get_reg(NULL, M68K_REG_PC),
                m68k_get_reg(NULL, M68K_REG_SR));
        exit(1);
    }
}

static void put16(uint32_t address, uint16_t value)
{
    check_address(address, 2u);
    memory[address] = (uint8_t)(value >> 8);
    memory[address + 1u] = (uint8_t)value;
}

static void put32(uint32_t address, uint32_t value)
{
    put16(address, (uint16_t)(value >> 16));
    put16(address + 2u, (uint16_t)value);
}

static uint16_t get16(uint32_t address)
{
    check_address(address, 2u);
    return (uint16_t)(((uint16_t)memory[address] << 8) |
                      memory[address + 1u]);
}

static uint32_t get32(uint32_t address)
{
    return ((uint32_t)get16(address) << 16) | get16(address + 2u);
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    check_address(address, 1u);
    return memory[address];
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return get16(address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return get32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    check_address(address, 1u);
    memory[address] = (uint8_t)value;
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    put16(address, (uint16_t)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (address == watched_write_address)
        ++watched_write_count;
    put32(address, value);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return get16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return get32(address);
}

static void begin_case(uint32_t pc)
{
    memset(memory, 0, sizeof(memory));
    watched_write_address = UINT32_MAX;
    watched_write_count = 0u;
    put32(0u, UINT32_C(0x00008000));
    put32(4u, pc);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68030);
    m68k_pulse_reset();
}

static void stop_handler(uint32_t address)
{
    /* MOVE.L D0,(A1); STOP #$2700 */
    put16(address, UINT16_C(0x2280));
    put16(address + 2u, UINT16_C(0x4e72));
    put16(address + 4u, UINT16_C(0x2700));
}

static void setup_one_level_fault_case(void)
{
    begin_case(UINT32_C(0x1000));
    put32(2u * 4u, UINT32_C(0x00002000));

    /* PMOVE (A0),CRP; PMOVE (A1),TC. */
    put16(UINT32_C(0x1000), UINT16_C(0xf010));
    put16(UINT32_C(0x1002), UINT16_C(0x4c00));
    put16(UINT32_C(0x1004), UINT16_C(0xf011));
    put16(UINT32_C(0x1006), UINT16_C(0x4000));
    put32(UINT32_C(0x2000), UINT32_C(0x7fff0002));
    put32(UINT32_C(0x2004), UINT32_C(0x00004000));
    put32(UINT32_C(0x2008), UINT32_C(0x80cc8000));

    /* Identity vectors, translated code/handler, page-table alias, stack. */
    put32(UINT32_C(0x4000), UINT32_C(0x00000001));
    put32(UINT32_C(0x4004), UINT32_C(0x00003001));
    put32(UINT32_C(0x4008), UINT32_C(0x00005001));
    put32(UINT32_C(0x4014), UINT32_C(0x00000000)); /* $5000 invalid */
    put32(UINT32_C(0x4018), UINT32_C(0x00004001)); /* $6000 -> table */
    put32(UINT32_C(0x401c), UINT32_C(0x00007001)); /* supervisor stack */

    m68k_set_reg(M68K_REG_A0, UINT32_C(0x2000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x2008));
    m68k_set_reg(M68K_REG_A2, UINT32_C(0x5000));
}

static void install_page_repair_handler(void)
{
    /* MOVE.L #$00009001,$00006014; PFLUSHA; RTE. */
    put16(UINT32_C(0x5000), UINT16_C(0x23fc));
    put16(UINT32_C(0x5002), UINT16_C(0x0000));
    put16(UINT32_C(0x5004), UINT16_C(0x9001));
    put16(UINT32_C(0x5006), UINT16_C(0x0000));
    put16(UINT32_C(0x5008), UINT16_C(0x6014));
    put16(UINT32_C(0x500a), UINT16_C(0xf000));
    put16(UINT32_C(0x500c), UINT16_C(0x2400));
    put16(UINT32_C(0x500e), UINT16_C(0x4e73));
}

static void build_base_frame(uint32_t address, uint16_t sr, uint32_t pc,
                             uint16_t format_vector, unsigned bytes)
{
    unsigned offset;

    for (offset = 0u; offset < bytes; offset += 2u)
        put16(address + offset, 0u);
    put16(address, sr);
    put32(address + 2u, pc);
    put16(address + 6u, format_vector);
}

static void check_frame_words(const char *test, uint32_t address,
                              const uint16_t *expected, unsigned count)
{
    unsigned word;

    for (word = 0u; word < count; ++word) {
        uint16_t actual = get16(address + word * 2u);
        if (actual != expected[word]) {
            fprintf(stderr, "FAIL %s: frame word %u: %04x != %04x\n",
                    test, word, actual, expected[word]);
            exit(1);
        }
    }
}

static void test_execution_translation_and_instructions(void)
{
    const char *name = "execution_translation_and_instructions";

    begin_case(UINT32_C(0x1000));

    /* PMOVE (A0),CRP; PMOVE (A1),TC. */
    put16(UINT32_C(0x1000), UINT16_C(0xf010));
    put16(UINT32_C(0x1002), UINT16_C(0x4c00));
    put16(UINT32_C(0x1004), UINT16_C(0xf011));
    put16(UINT32_C(0x1006), UINT16_C(0x4000));

    put32(UINT32_C(0x2000), UINT32_C(0x7fff0002));
    put32(UINT32_C(0x2004), UINT32_C(0x00004000));
    /* E=1, PS=12, IS=12, TIA=8: one short-format table level. */
    put32(UINT32_C(0x2008), UINT32_C(0x80cc8000));
    put32(UINT32_C(0x4004), UINT32_C(0x00003001)); /* $1000 -> $3000 */
    put32(UINT32_C(0x4014), UINT32_C(0x00006001)); /* $5000 -> $6000 */

    /*
     * PLOADW #5,(A2)
     * MOVE.L #$11223344,D0
     * MOVE.L D0,(A2)
     * PTESTR #5,(A2),#7,A3
     * PMOVE MMUSR,(A4)
     * PFLUSHA
     * STOP #$2700
     */
    put16(UINT32_C(0x3008), UINT16_C(0xf012));
    put16(UINT32_C(0x300a), UINT16_C(0x2015));
    put16(UINT32_C(0x300c), UINT16_C(0x203c));
    put16(UINT32_C(0x300e), UINT16_C(0x1122));
    put16(UINT32_C(0x3010), UINT16_C(0x3344));
    put16(UINT32_C(0x3012), UINT16_C(0x2480));
    put16(UINT32_C(0x3014), UINT16_C(0xf012));
    put16(UINT32_C(0x3016), UINT16_C(0x9f71));
    put16(UINT32_C(0x3018), UINT16_C(0xf014));
    put16(UINT32_C(0x301a), UINT16_C(0x6200));
    put16(UINT32_C(0x301c), UINT16_C(0xf000));
    put16(UINT32_C(0x301e), UINT16_C(0x2400));
    put16(UINT32_C(0x3020), UINT16_C(0x4e72));
    put16(UINT32_C(0x3022), UINT16_C(0x2700));

    m68k_set_reg(M68K_REG_A0, UINT32_C(0x2000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x2008));
    m68k_set_reg(M68K_REG_A2, UINT32_C(0x5000));
    m68k_set_reg(M68K_REG_A4, UINT32_C(0x5004));
    (void)m68k_execute(10000);

    CHECK(name, get32(UINT32_C(0x6000)) == UINT32_C(0x11223344));
    CHECK(name, get16(UINT32_C(0x6004)) == UINT16_C(0x0201));
    CHECK(name, (get32(UINT32_C(0x4004)) & 8u) != 0u);
    CHECK(name, (get32(UINT32_C(0x4014)) & UINT32_C(0x18)) ==
                UINT32_C(0x18));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_A3) == UINT32_C(0x4014));
    ++tests_run;
}

static void test_transparent_ptest_level_zero(void)
{
    const char *name = "transparent_ptest_level_zero";

    begin_case(UINT32_C(0x1000));
    /* PMOVE (A0),TT0; PTESTR #5,(A1),#0; PMOVE MMUSR,(A2); STOP. */
    put16(UINT32_C(0x1000), UINT16_C(0xf010));
    put16(UINT32_C(0x1002), UINT16_C(0x0800));
    put16(UINT32_C(0x1004), UINT16_C(0xf011));
    put16(UINT32_C(0x1006), UINT16_C(0x8215));
    put16(UINT32_C(0x1008), UINT16_C(0xf012));
    put16(UINT32_C(0x100a), UINT16_C(0x6200));
    put16(UINT32_C(0x100c), UINT16_C(0x4e72));
    put16(UINT32_C(0x100e), UINT16_C(0x2700));
    put32(UINT32_C(0x3000), UINT32_C(0x12008150));

    m68k_set_reg(M68K_REG_A0, UINT32_C(0x3000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x12345000));
    m68k_set_reg(M68K_REG_A2, UINT32_C(0x5000));
    (void)m68k_execute(4000);
    CHECK(name, get16(UINT32_C(0x5000)) == UINT16_C(0x0040));
    ++tests_run;
}

static void test_cross_page_long_write(void)
{
    const char *name = "cross_page_long_write";

    begin_case(UINT32_C(0x1000));
    put16(UINT32_C(0x1000), UINT16_C(0xf010));
    put16(UINT32_C(0x1002), UINT16_C(0x4c00));
    put16(UINT32_C(0x1004), UINT16_C(0xf011));
    put16(UINT32_C(0x1006), UINT16_C(0x4000));
    put32(UINT32_C(0x2000), UINT32_C(0x7fff0002));
    put32(UINT32_C(0x2004), UINT32_C(0x00004000));
    put32(UINT32_C(0x2008), UINT32_C(0x80cc8000));
    put32(UINT32_C(0x4004), UINT32_C(0x00003001));
    put32(UINT32_C(0x4014), UINT32_C(0x00006001));
    put32(UINT32_C(0x4018), UINT32_C(0x00008001));

    /* MOVE.L #$a1b2c3d4,D0; MOVE.L D0,(A2); STOP #$2700. */
    put16(UINT32_C(0x3008), UINT16_C(0x203c));
    put16(UINT32_C(0x300a), UINT16_C(0xa1b2));
    put16(UINT32_C(0x300c), UINT16_C(0xc3d4));
    put16(UINT32_C(0x300e), UINT16_C(0x2480));
    put16(UINT32_C(0x3010), UINT16_C(0x4e72));
    put16(UINT32_C(0x3012), UINT16_C(0x2700));

    m68k_set_reg(M68K_REG_A0, UINT32_C(0x2000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x2008));
    m68k_set_reg(M68K_REG_A2, UINT32_C(0x5ffe));
    (void)m68k_execute(5000);

    CHECK(name, get16(UINT32_C(0x6ffe)) == UINT16_C(0xa1b2));
    CHECK(name, get16(UINT32_C(0x8000)) == UINT16_C(0xc3d4));
    CHECK(name, (get32(UINT32_C(0x4014)) & UINT32_C(0x18)) ==
                UINT32_C(0x18));
    CHECK(name, (get32(UINT32_C(0x4018)) & UINT32_C(0x18)) ==
                UINT32_C(0x18));
    ++tests_run;
}

static void test_configuration_exception_frame(void)
{
    const char *name = "configuration_exception_frame";

    begin_case(UINT32_C(0x1000));
    put32(56u * 4u, UINT32_C(0x2000));
    put16(UINT32_C(0x1000), UINT16_C(0xf010));
    put16(UINT32_C(0x1002), UINT16_C(0x4000));
    put32(UINT32_C(0x3000), UINT32_C(0x80000000)); /* E with reserved PS */
    stop_handler(UINT32_C(0x2000));
    m68k_set_reg(M68K_REG_A0, UINT32_C(0x3000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x4000));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0xcafebabe));
    (void)m68k_execute(4000);

    CHECK(name, get32(UINT32_C(0x4000)) == UINT32_C(0xcafebabe));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x7ff4));
    CHECK(name, get32(UINT32_C(0x7ff6)) == UINT32_C(0x1004));
    CHECK(name, get16(UINT32_C(0x7ffa)) == UINT16_C(0x20e0));
    CHECK(name, get32(UINT32_C(0x7ffc)) == UINT32_C(0x1000));
    ++tests_run;
}

static void test_rejects_68851_only_command(void)
{
    const char *name = "rejects_68851_only_command";

    begin_case(UINT32_C(0x1000));
    put32(11u * 4u, UINT32_C(0x2000));
    put16(UINT32_C(0x1000), UINT16_C(0xf000));
    put16(UINT32_C(0x1002), UINT16_C(0xa000)); /* PFLUSHR class: not 68030 */
    stop_handler(UINT32_C(0x2000));
    m68k_set_reg(M68K_REG_A1, UINT32_C(0x4000));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0x68851030));
    (void)m68k_execute(4000);

    CHECK(name, get32(UINT32_C(0x4000)) == UINT32_C(0x68851030));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x7ff8));
    CHECK(name, get32(UINT32_C(0x7ffa)) == UINT32_C(0x1000));
    CHECK(name, get16(UINT32_C(0x7ffe)) == UINT16_C(0x002c));
    ++tests_run;
}

static void test_format_b_demand_page_restart(void)
{
    const char *name = "format_b_demand_page_restart";
    static const uint16_t expected_frame[46] = {
        [0] = UINT16_C(0x2700),
        [2] = UINT16_C(0x1008),
        [3] = UINT16_C(0xb008),
        [5] = UINT16_C(0x0145),
        [9] = UINT16_C(0x5000),
        [19] = UINT16_C(0x100a)
    };

    setup_one_level_fault_case();
    install_page_repair_handler();
    /* MOVE.L (A2)+,D0; STOP #$2700 at translated logical $1008. */
    put16(UINT32_C(0x3008), UINT16_C(0x201a));
    put16(UINT32_C(0x300a), UINT16_C(0x4e72));
    put16(UINT32_C(0x300c), UINT16_C(0x2700));
    put32(UINT32_C(0x9000), UINT32_C(0x11223344));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0xdeadbeef));

    (void)m68k_execute(20000);

    CHECK(name, m68k_get_reg(NULL, M68K_REG_D0) == UINT32_C(0x11223344));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_A2) == UINT32_C(0x5004));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x8000));
    CHECK(name, get16(FORMAT_B_FRAME) == UINT16_C(0x2700));
    CHECK(name, get32(FORMAT_B_FRAME + 2u) == UINT32_C(0x1008));
    CHECK(name, get16(FORMAT_B_FRAME + 6u) == UINT16_C(0xb008));
    CHECK(name, get16(FORMAT_B_FRAME + 8u) == 0u);
    CHECK(name, get16(FORMAT_B_FRAME + 0x0au) == UINT16_C(0x0145));
    CHECK(name, get32(FORMAT_B_FRAME + 0x10u) == UINT32_C(0x5000));
    CHECK(name, get32(FORMAT_B_FRAME + 0x18u) == 0u);
    CHECK(name, get32(FORMAT_B_FRAME + 0x24u) == UINT32_C(0x100a));
    CHECK(name, get32(FORMAT_B_FRAME + 0x2cu) == 0u);
    CHECK(name, get16(FORMAT_B_FRAME + 0x36u) == 0u);
    CHECK(name, get16(FORMAT_B_FRAME + 0x5au) == 0u);
    check_frame_words(name, FORMAT_B_FRAME, expected_frame, 46u);
    ++tests_run;
}

static void test_user_format_b_restart_and_stack_swap(void)
{
    const char *name = "user_format_b_restart_and_stack_swap";

    setup_one_level_fault_case();
    install_page_repair_handler();

    /* Enter user mode, fault a postincrement read, then spin after recovery. */
    put16(UINT32_C(0x3008), UINT16_C(0x46fc));
    put16(UINT32_C(0x300a), UINT16_C(0x0000));
    put16(UINT32_C(0x300c), UINT16_C(0x201a));
    put16(UINT32_C(0x300e), UINT16_C(0x60fe));
    put32(UINT32_C(0x9000), UINT32_C(0x89abcdef));
    m68k_set_reg(M68K_REG_USP, UINT32_C(0x6ff0));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0x01020304));

    (void)m68k_execute(20000);

    CHECK(name, m68k_get_reg(NULL, M68K_REG_D0) == UINT32_C(0x89abcdef));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_A2) == UINT32_C(0x5004));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x6ff0));
    CHECK(name, (m68k_get_reg(NULL, M68K_REG_SR) & UINT32_C(0x2000)) == 0u);
    CHECK(name, get16(FORMAT_B_FRAME) == 0u);
    CHECK(name, get32(FORMAT_B_FRAME + 2u) == UINT32_C(0x100c));
    CHECK(name, get16(FORMAT_B_FRAME + 6u) == UINT16_C(0xb008));
    CHECK(name, get16(FORMAT_B_FRAME + 0x0au) == UINT16_C(0x0141));
    CHECK(name, get32(FORMAT_B_FRAME + 0x10u) == UINT32_C(0x5000));
    ++tests_run;
}

static void test_format_b_replay_suppresses_completed_movem_write(void)
{
    const char *name = "format_b_replay_suppresses_completed_movem_write";

    setup_one_level_fault_case();
    /* Move the table alias to logical $4000, map $5000, leave $6000 bad. */
    put32(UINT32_C(0x4010), UINT32_C(0x00004001));
    put32(UINT32_C(0x4014), UINT32_C(0x00009001));
    put32(UINT32_C(0x4018), UINT32_C(0x00000000));

    /* MOVE.L #$0000A001,$00004018; PFLUSHA; RTE. */
    put16(UINT32_C(0x5000), UINT16_C(0x23fc));
    put16(UINT32_C(0x5002), UINT16_C(0x0000));
    put16(UINT32_C(0x5004), UINT16_C(0xa001));
    put16(UINT32_C(0x5006), UINT16_C(0x0000));
    put16(UINT32_C(0x5008), UINT16_C(0x4018));
    put16(UINT32_C(0x500a), UINT16_C(0xf000));
    put16(UINT32_C(0x500c), UINT16_C(0x2400));
    put16(UINT32_C(0x500e), UINT16_C(0x4e73));

    /* MOVEM.L D0-D1,(A2); STOP #$2700. */
    put16(UINT32_C(0x3008), UINT16_C(0x48d2));
    put16(UINT32_C(0x300a), UINT16_C(0x0003));
    put16(UINT32_C(0x300c), UINT16_C(0x4e72));
    put16(UINT32_C(0x300e), UINT16_C(0x2700));
    m68k_set_reg(M68K_REG_A2, UINT32_C(0x5ffc));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0x11112222));
    m68k_set_reg(M68K_REG_D1, UINT32_C(0x33334444));
    watched_write_address = UINT32_C(0x9ffc);

    (void)m68k_execute(30000);

    CHECK(name, get32(UINT32_C(0x9ffc)) == UINT32_C(0x11112222));
    CHECK(name, get32(UINT32_C(0xa000)) == UINT32_C(0x33334444));
    CHECK(name, watched_write_count == 1u);
    CHECK(name, get16(FORMAT_B_FRAME + 6u) == UINT16_C(0xb008));
    CHECK(name, get32(FORMAT_B_FRAME + 0x10u) == UINT32_C(0x6000));
    CHECK(name, get32(FORMAT_B_FRAME + 0x18u) == UINT32_C(0x33334444));
    ++tests_run;
}

static void test_format_b_software_completed_read(void)
{
    const char *name = "format_b_software_completed_read";

    setup_one_level_fault_case();

    /* Handler supplies DIB, clears only DF in the SSW, and returns. */
    put16(UINT32_C(0x5000), UINT16_C(0x2f7c));
    put16(UINT32_C(0x5002), UINT16_C(0xcafe));
    put16(UINT32_C(0x5004), UINT16_C(0xbabe));
    put16(UINT32_C(0x5006), UINT16_C(0x002c));
    put16(UINT32_C(0x5008), UINT16_C(0x026f));
    put16(UINT32_C(0x500a), UINT16_C(0xfeff));
    put16(UINT32_C(0x500c), UINT16_C(0x000a));
    put16(UINT32_C(0x500e), UINT16_C(0x4e73));

    put16(UINT32_C(0x3008), UINT16_C(0x201a)); /* MOVE.L (A2)+,D0 */
    put16(UINT32_C(0x300a), UINT16_C(0x4e72));
    put16(UINT32_C(0x300c), UINT16_C(0x2700));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0x01020304));

    (void)m68k_execute(20000);

    CHECK(name, m68k_get_reg(NULL, M68K_REG_D0) == UINT32_C(0xcafebabe));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_A2) == UINT32_C(0x5004));
    CHECK(name, get16(FORMAT_B_FRAME + 0x0au) == UINT16_C(0x0045));
    CHECK(name, get32(FORMAT_B_FRAME + 0x2cu) == UINT32_C(0xcafebabe));
    CHECK(name, get32(UINT32_C(0x4014)) == 0u);
    ++tests_run;
}

static void test_format_b_instruction_fetch_restart(void)
{
    const char *name = "format_b_instruction_fetch_restart";

    setup_one_level_fault_case();
    install_page_repair_handler();

    /* JMP $5000 faults when the next opcode is consumed at a boundary. */
    put16(UINT32_C(0x3008), UINT16_C(0x4ef9));
    put16(UINT32_C(0x300a), UINT16_C(0x0000));
    put16(UINT32_C(0x300c), UINT16_C(0x5000));
    put16(UINT32_C(0x9000), UINT16_C(0x702a)); /* MOVEQ #42,D0 */
    put16(UINT32_C(0x9002), UINT16_C(0x4e72));
    put16(UINT32_C(0x9004), UINT16_C(0x2700));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0xffffffff));

    (void)m68k_execute(20000);

    CHECK(name, m68k_get_reg(NULL, M68K_REG_D0) == UINT32_C(42));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x8000));
    CHECK(name, get16(FORMAT_B_FRAME) == UINT16_C(0x2700));
    CHECK(name, get32(FORMAT_B_FRAME + 2u) == UINT32_C(0x5000));
    CHECK(name, get16(FORMAT_B_FRAME + 6u) == UINT16_C(0xb008));
    CHECK(name, get16(FORMAT_B_FRAME + 0x0au) == UINT16_C(0x5000));
    CHECK(name, get32(FORMAT_B_FRAME + 0x10u) == UINT32_C(0x5000));
    CHECK(name, get32(FORMAT_B_FRAME + 0x24u) == UINT32_C(0x5000));
    CHECK(name, get16(FORMAT_B_FRAME + 0x36u) == 0u);
    ++tests_run;
}

static void test_format_a_lastwrite_restart(void)
{
    const char *name = "format_a_lastwrite_restart";
    static const uint16_t expected_frame[16] = {
        [0] = UINT16_C(0x2708),
        [2] = UINT16_C(0x100a),
        [3] = UINT16_C(0xa008),
        [4] = UINT16_C(0x0100),
        [5] = UINT16_C(0x0105),
        [9] = UINT16_C(0x5000),
        [12] = UINT16_C(0x8123),
        [13] = UINT16_C(0x4567)
    };

    setup_one_level_fault_case();
    install_page_repair_handler();

    /* MOVE.L D0,(A2)+ is complete except for its final pending write. */
    put16(UINT32_C(0x3008), UINT16_C(0x24c0));
    put16(UINT32_C(0x300a), UINT16_C(0x4e72));
    put16(UINT32_C(0x300c), UINT16_C(0x2700));
    m68k_set_reg(M68K_REG_D0, UINT32_C(0x81234567));

    (void)m68k_execute(20000);

    CHECK(name, get32(UINT32_C(0x9000)) == UINT32_C(0x81234567));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_A2) == UINT32_C(0x5004));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x8000));
    CHECK(name, get16(FORMAT_A_FRAME) == UINT16_C(0x2708));
    CHECK(name, get32(FORMAT_A_FRAME + 2u) == UINT32_C(0x100a));
    CHECK(name, get16(FORMAT_A_FRAME + 6u) == UINT16_C(0xa008));
    CHECK(name, get16(FORMAT_A_FRAME + 8u) == UINT16_C(0x0100));
    CHECK(name, get16(FORMAT_A_FRAME + 0x0au) == UINT16_C(0x0105));
    CHECK(name, get32(FORMAT_A_FRAME + 0x10u) == UINT32_C(0x5000));
    CHECK(name, get32(FORMAT_A_FRAME + 0x18u) == UINT32_C(0x81234567));
    check_frame_words(name, FORMAT_A_FRAME, expected_frame, 16u);
    ++tests_run;
}

static void test_format_a_rte_replays_pending_write(void)
{
    const char *name = "format_a_rte_replays_pending_write";

    begin_case(UINT32_C(0x1000));
    put16(UINT32_C(0x1000), UINT16_C(0x2e7c));
    put16(UINT32_C(0x1002), UINT16_C(0x0000));
    put16(UINT32_C(0x1004), UINT16_C(0x7fe0));
    put16(UINT32_C(0x1006), UINT16_C(0x4e73));
    put16(UINT32_C(0x1200), UINT16_C(0x4e72));
    put16(UINT32_C(0x1202), UINT16_C(0x2700));

    build_base_frame(FORMAT_A_FRAME, UINT16_C(0x2700),
                     UINT32_C(0x1200), UINT16_C(0xa008), 32u);
    put16(FORMAT_A_FRAME + 8u, UINT16_C(0x0100));
    put16(FORMAT_A_FRAME + 0x0au, UINT16_C(0x0115));
    put32(FORMAT_A_FRAME + 0x10u, UINT32_C(0x4001));
    put32(FORMAT_A_FRAME + 0x18u, UINT32_C(0x112233a5));
    put16(UINT32_C(0x4000), UINT16_C(0x5555));

    (void)m68k_execute(4000);

    CHECK(name, get16(UINT32_C(0x4000)) == UINT16_C(0x55a5));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x8000));
    CHECK(name, m68k_get_reg(NULL, M68K_REG_PC) == UINT32_C(0x1204));
    ++tests_run;
}

static void test_format_b_rejects_foreign_version(void)
{
    const char *name = "format_b_rejects_foreign_version";

    begin_case(UINT32_C(0x1000));
    put32(14u * 4u, UINT32_C(0x2000));
    put16(UINT32_C(0x1000), UINT16_C(0x2e7c));
    put16(UINT32_C(0x1002), UINT16_C(0x0000));
    put16(UINT32_C(0x1004), UINT16_C(0x7fa4));
    put16(UINT32_C(0x1006), UINT16_C(0x4e73));
    stop_handler(UINT32_C(0x2000));

    build_base_frame(FORMAT_B_FRAME, UINT16_C(0x2700),
                     UINT32_C(0x1200), UINT16_C(0xb008), 92u);
    put16(FORMAT_B_FRAME + 0x36u, UINT16_C(0x1000));

    (void)m68k_execute(4000);

    CHECK(name, m68k_get_reg(NULL, M68K_REG_SP) == UINT32_C(0x7f9c));
    CHECK(name, get16(UINT32_C(0x7fa2)) == UINT16_C(0x0038));
    CHECK(name, get16(FORMAT_B_FRAME + 6u) == UINT16_C(0xb008));
    CHECK(name, get16(FORMAT_B_FRAME + 0x36u) == UINT16_C(0x1000));
    ++tests_run;
}

int main(void)
{
    test_execution_translation_and_instructions();
    test_transparent_ptest_level_zero();
    test_cross_page_long_write();
    test_configuration_exception_frame();
    test_rejects_68851_only_command();
    test_format_b_demand_page_restart();
    test_user_format_b_restart_and_stack_swap();
    test_format_b_replay_suppresses_completed_movem_write();
    test_format_b_software_completed_read();
    test_format_b_instruction_fetch_restart();
    test_format_a_lastwrite_restart();
    test_format_a_rte_replays_pending_write();
    test_format_b_rejects_foreign_version();
    printf("PASS: %u Musashi/MC68030 PMMU integration groups\n", tests_run);
    return 0;
}
