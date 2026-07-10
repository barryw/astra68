// Astra 68 pre-Harte CPU/SoC validation.
//
// Phase 1 focuses on memory destination effective-address writeback and
// byte-lane behavior, because absolute indexed byte stores already escaped.
#include <stdint.h>
#include "vesta.h"

#define SCRATCH_BASE 0x01ff9100u
#define SCRATCH      ((volatile uint8_t *)SCRATCH_BASE)
#define EXC_REC_BASE (SCRATCH_BASE + 0x170u)
#define EXC_RECOVERY_PC (EXC_REC_BASE + 0x14u)
#define EXC_FAKE_STACK (SCRATCH_BASE + 0x300u)
#define EXC_ALT_VBR (SCRATCH_BASE + 0x400u)
#define IRQ_SIM_REQ (SCRATCH_BASE + 0x500u)
#define STACK_TEST_BASE (SCRATCH_BASE + 0x600u)
#define MOVES_TEST_BASE (SCRATCH_BASE + 0x700u)
#define BOUNDS_TEST_BASE (SCRATCH_BASE + 0x720u)
#define CAS2_TEST_BASE (SCRATCH_BASE + 0x740u)
#define RETURN_TEST_BASE (SCRATCH_BASE + 0x800u)
#define BCD_TEST_BASE (SCRATCH_BASE + 0x900u)
#define PACK_TEST_BASE (SCRATCH_BASE + 0xa00u)
#define MOVEP_TEST_BASE (SCRATCH_BASE + 0xb00u)
#define BITFIELD_TEST_BASE (SCRATCH_BASE + 0xc00u)
#define FULLFMT_TEST_BASE (SCRATCH_BASE + 0xe00u)
#define SHIFT_TEST_BASE (SCRATCH_BASE + 0xf00u)
#define COND_TEST_BASE (SCRATCH_BASE + 0x1000u)
#define BITOP_TEST_BASE (SCRATCH_BASE + 0x1200u)
#define UNARY_TEST_BASE (SCRATCH_BASE + 0x1400u)
#define IMM_TEST_BASE (SCRATCH_BASE + 0x1500u)
#define XMEM_TEST_BASE (SCRATCH_BASE + 0x1600u)
#define MUL_DIV_TEST_BASE (SCRATCH_BASE + 0x1800u)
#define CHK_TEST_BASE (SCRATCH_BASE + 0x1a00u)

static volatile uint32_t g_sum;

extern void _h_default(void);
extern void _h_recover(void);
extern void coretest_movem_fullshape_asm(void);
extern void coretest_movem_ramjsr_asm(void);
extern void coretest_movem_ramadd_asm(void);

void *memcpy(void *d, const void *s, unsigned long n)
{
    char *dd = (char *)d;
    const char *ss = (const char *)s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memset(void *d, int c, unsigned long n)
{
    char *dd = (char *)d;
    while (n--) *dd++ = (char)c;
    return d;
}

static void uart_putc(char c)
{
    while (!(VESTA->UART_STATUS & UART_TX_READY)) {}
    VESTA->UART_DATA = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_hex32(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> i) & 0xfu;
        uart_putc((char)(d < 10 ? ('0' + d) : ('A' + d - 10)));
    }
}

static void delay_poll_window(void)
{
    for (volatile uint32_t delay = 0; delay < 10000u; ++delay) {}
}

static void stop_fail(uint32_t id, uint32_t got, uint32_t exp)
{
    for (;;) {
        uart_puts("CORETEST FAIL id=");
        uart_hex32(id);
        uart_puts(" got=");
        uart_hex32(got);
        uart_puts(" exp=");
        uart_hex32(exp);
        uart_putc('\n');
        delay_poll_window();
    }
}

static void mark(uint32_t id, uint32_t value)
{
    g_sum = ((g_sum << 5) | (g_sum >> 27)) ^ id ^ value;
}

static uint32_t rd32(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

static uint16_t rd16(uint32_t addr)
{
    return *(volatile uint16_t *)addr;
}

static uint8_t rd8(uint32_t addr)
{
    return *(volatile uint8_t *)addr;
}

static void wr32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

static void arm_exception_recovery(uint32_t vector_offset)
{
    wr32(EXC_REC_BASE + 0x00, 0u);
    wr32(EXC_REC_BASE + 0x04, 0u);
    wr32(EXC_REC_BASE + 0x08, 0u);
    wr32(EXC_REC_BASE + 0x0c, 0u);
    wr32(EXC_REC_BASE + 0x10, 0u);
    wr32(EXC_RECOVERY_PC, 0u);
    wr32(EXC_REC_BASE + 0x18, vector_offset);
}

static void chk32(uint32_t id, uint32_t got, uint32_t exp)
{
    mark(id, got);
    if (got != exp) stop_fail(id, got, exp);
}

static void chk16(uint32_t id, uint32_t got, uint32_t exp)
{
    mark(id, got);
    if ((got & 0xffffu) != (exp & 0xffffu)) stop_fail(id, got & 0xffffu, exp & 0xffffu);
}

static void chk8(uint32_t id, uint32_t got, uint32_t exp)
{
    mark(id, got);
    if ((got & 0xffu) != (exp & 0xffu)) stop_fail(id, got & 0xffu, exp & 0xffu);
}

static void chk_exception_frame(uint32_t id, uint32_t vector_offset,
                                uint32_t frame_format,
                                uint32_t sr_mask, uint32_t sr_exp)
{
    chk32(id + 0x00u, rd32(EXC_REC_BASE + 0x00), 1u);
    chk32(id + 0x04u, rd32(EXC_REC_BASE + 0x0c) & 0x0fffu, vector_offset);
    chk32(id + 0x08u, rd32(EXC_REC_BASE + 0x0c) & 0xf000u, frame_format);
    chk32(id + 0x0cu, rd32(EXC_REC_BASE + 0x04) & sr_mask, sr_exp);
    chk32(id + 0x10u, rd32(EXC_REC_BASE + 0x10) & 1u, 0u);
    chk32(id + 0x14u, rd32(EXC_REC_BASE + 0x08) & 1u, 0u);
}

static void chk_access_fault_frame(uint32_t id, uint32_t vector_offset,
                                   uint32_t sr_mask, uint32_t sr_exp,
                                   uint32_t stacked_pc_low_bit)
{
    uint32_t format = rd32(EXC_REC_BASE + 0x0c) & 0xf000u;

    chk32(id + 0x00u, rd32(EXC_REC_BASE + 0x00), 1u);
    chk32(id + 0x04u, rd32(EXC_REC_BASE + 0x0c) & 0x0fffu, vector_offset);
    mark(id + 0x08u, format);
    if (format != 0xa000u && format != 0xb000u) {
        stop_fail(id + 0x08u, format, 0xa000b000u);
    }
    chk32(id + 0x0cu, rd32(EXC_REC_BASE + 0x04) & sr_mask, sr_exp);
    chk32(id + 0x10u, rd32(EXC_REC_BASE + 0x10) & 1u, 0u);
    chk32(id + 0x14u, rd32(EXC_REC_BASE + 0x08) & 1u, stacked_pc_low_bit);
}

static void test_aligned_long(void)
{
    wr32(SCRATCH_BASE + 0x00, 0x11223344u);
    chk32(0x00010001u, rd32(SCRATCH_BASE + 0x00), 0x11223344u);
}

static void test_byte_lanes_c(void)
{
    wr32(SCRATCH_BASE + 0x10, 0x11223344u);
    SCRATCH[0x10] = 0xaau;
    chk32(0x00020000u, rd32(SCRATCH_BASE + 0x10), 0xaa223344u);

    wr32(SCRATCH_BASE + 0x10, 0x11223344u);
    SCRATCH[0x11] = 0xbbu;
    chk32(0x00020001u, rd32(SCRATCH_BASE + 0x10), 0x11bb3344u);

    wr32(SCRATCH_BASE + 0x10, 0x11223344u);
    SCRATCH[0x12] = 0xccu;
    chk32(0x00020002u, rd32(SCRATCH_BASE + 0x10), 0x1122cc44u);

    wr32(SCRATCH_BASE + 0x10, 0x11223344u);
    SCRATCH[0x13] = 0xddu;
    chk32(0x00020003u, rd32(SCRATCH_BASE + 0x10), 0x112233ddu);
}

static void test_word_lanes_c(void)
{
    wr32(SCRATCH_BASE + 0x20, 0x11223344u);
    *(volatile uint16_t *)(SCRATCH_BASE + 0x20) = 0xaabbu;
    chk32(0x00030000u, rd32(SCRATCH_BASE + 0x20), 0xaabb3344u);

    wr32(SCRATCH_BASE + 0x20, 0x11223344u);
    *(volatile uint16_t *)(SCRATCH_BASE + 0x22) = 0xccddu;
    chk32(0x00030002u, rd32(SCRATCH_BASE + 0x20), 0x1122ccddu);
}

static void test_unaligned_lanes_asm(void)
{
    uint32_t got;

    wr32(SCRATCH_BASE + 0x28, 0x11223344u);
    __asm__ volatile("move.w #0xa1b2,0x01ff9129" : : : "memory");
    chk32(0x00031001u, rd32(SCRATCH_BASE + 0x28), 0x11a1b244u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w 0x01ff9129,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "memory");
    chk32(0x00031002u, got, 0xa1b2u);

    wr32(SCRATCH_BASE + 0x28, 0x11223344u);
    wr32(SCRATCH_BASE + 0x2c, 0x55667788u);
    __asm__ volatile("move.w #0xc3d4,0x01ff912b" : : : "memory");
    chk32(0x00031003u, rd32(SCRATCH_BASE + 0x28), 0x112233c3u);
    chk32(0x00031004u, rd32(SCRATCH_BASE + 0x2c), 0xd4667788u);

    wr32(SCRATCH_BASE + 0x30, 0x11223344u);
    wr32(SCRATCH_BASE + 0x34, 0x55667788u);
    __asm__ volatile("move.l #0xa1b2c3d4,0x01ff9131" : : : "memory");
    chk32(0x00031011u, rd32(SCRATCH_BASE + 0x30), 0x11a1b2c3u);
    chk32(0x00031012u, rd32(SCRATCH_BASE + 0x34), 0xd4667788u);

    __asm__ volatile(
        "move.l 0x01ff9131,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "memory");
    chk32(0x00031013u, got, 0xa1b2c3d4u);

    wr32(SCRATCH_BASE + 0x30, 0x11223344u);
    wr32(SCRATCH_BASE + 0x34, 0x55667788u);
    __asm__ volatile("move.l #0xa1b2c3d4,0x01ff9132" : : : "memory");
    chk32(0x00031021u, rd32(SCRATCH_BASE + 0x30), 0x1122a1b2u);
    chk32(0x00031022u, rd32(SCRATCH_BASE + 0x34), 0xc3d47788u);

    wr32(SCRATCH_BASE + 0x30, 0x11223344u);
    wr32(SCRATCH_BASE + 0x34, 0x55667788u);
    __asm__ volatile("move.l #0xa1b2c3d4,0x01ff9133" : : : "memory");
    chk32(0x00031031u, rd32(SCRATCH_BASE + 0x30), 0x112233a1u);
    chk32(0x00031032u, rd32(SCRATCH_BASE + 0x34), 0xb2c3d488u);
}

static void test_absolute_indexed_stores(void)
{
    wr32(SCRATCH_BASE + 0x40, 0x00000000u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "move.b #0x5a,0x01ff9140(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk8(0x00040002u, rd8(SCRATCH_BASE + 0x42), 0x5au);
    chk32(0x00040012u, rd32(SCRATCH_BASE + 0x40), 0x00005a00u);

    wr32(SCRATCH_BASE + 0x40, 0x00000000u);
    __asm__ volatile(
        "moveq #0x6b,%%d0\n\t"
        "move.l #3,%%d4\n\t"
        "move.b %%d0,0x01ff9140(%%d4:l)"
        :
        :
        : "d0", "d4", "memory");
    chk8(0x00040003u, rd8(SCRATCH_BASE + 0x43), 0x6bu);
    chk32(0x00040013u, rd32(SCRATCH_BASE + 0x40), 0x0000006bu);

    wr32(SCRATCH_BASE + 0x44, 0x00000000u);
    __asm__ volatile(
        "move.l #4,%%d4\n\t"
        "move.w #0x789a,0x01ff9140(%%d4:l)\n\t"
        "move.l #6,%%d4\n\t"
        "move.w #0xbcde,0x01ff9140(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk16(0x00040024u, rd16(SCRATCH_BASE + 0x44), 0x789au);
    chk16(0x00040026u, rd16(SCRATCH_BASE + 0x46), 0xbcdeu);
    chk32(0x00040034u, rd32(SCRATCH_BASE + 0x44), 0x789abcdeu);

    wr32(SCRATCH_BASE + 0x48, 0x00000000u);
    __asm__ volatile(
        "move.l #8,%%d4\n\t"
        "move.l #0x12345678,0x01ff9140(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk32(0x00040048u, rd32(SCRATCH_BASE + 0x48), 0x12345678u);

    wr32(SCRATCH_BASE + 0x70, 0x00000000u);
    __asm__ volatile(
        "move.l #-1,%%d4\n\t"
        "move.b #0x91,0x01ff9171(%%d4:w)"
        :
        :
        : "d4", "memory");
    chk32(0x00040070u, rd32(SCRATCH_BASE + 0x70), 0x91000000u);

    wr32(SCRATCH_BASE + 0x70, 0x00000000u);
    __asm__ volatile(
        "move.l #0x00010002,%%d4\n\t"
        "move.b #0x92,0x01ff9170(%%d4:w)"
        :
        :
        : "d4", "memory");
    chk32(0x00040072u, rd32(SCRATCH_BASE + 0x70), 0x00009200u);

    wr32(SCRATCH_BASE + 0x74, 0x00000000u);
    __asm__ volatile(
        "move.l #-1,%%d4\n\t"
        "move.b #0x93,0x01ff9175(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk32(0x00040074u, rd32(SCRATCH_BASE + 0x74), 0x93000000u);

    wr32(SCRATCH_BASE + 0x70, 0x00000000u);
    __asm__ volatile(
        "movea.l #3,%%a1\n\t"
        "move.b #0x94,0x01ff9170(%%a1:l)"
        :
        :
        : "a1", "memory");
    chk32(0x00040073u, rd32(SCRATCH_BASE + 0x70), 0x00000094u);

    wr32(SCRATCH_BASE + 0x74, 0x00000000u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "move.b #0x95,0x01ff9170(%%d4:l:2)"
        :
        :
        : "d4", "memory");
    chk32(0x00040078u, rd32(SCRATCH_BASE + 0x74), 0x95000000u);
}

static void test_full_format_indexed_memory_ops(void)
{
    uint32_t got;

    wr32(SCRATCH_BASE + 0x50, 0x11223344u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "move.b 0x01ff9150(%%d4:l),%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d4", "memory");
    chk8(0x00070002u, got, 0x33u);

    wr32(SCRATCH_BASE + 0x50, 0x11223344u);
    __asm__ volatile(
        "move.l #3,%%d4\n\t"
        "add.b #0x07,0x01ff9150(%%d4:l)"
        :
        :
        : "d4", "cc", "memory");
    chk32(0x00070013u, rd32(SCRATCH_BASE + 0x50), 0x1122334bu);

    wr32(SCRATCH_BASE + 0x60, 0xaabbccddu);
    __asm__ volatile(
        "move.l #1,%%d4\n\t"
        "clr.b 0x01ff9160(%%d4:l)"
        :
        :
        : "d4", "cc", "memory");
    chk32(0x00070021u, rd32(SCRATCH_BASE + 0x60), 0xaa00ccddu);

    wr32(SCRATCH_BASE + 0x60, 0x00000500u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "neg.b 0x01ff9160(%%d4:l)"
        :
        :
        : "d4", "cc", "memory");
    chk32(0x00070032u, rd32(SCRATCH_BASE + 0x60), 0x0000fb00u);

    wr32(SCRATCH_BASE + 0x50, 0x11223344u);
    wr32(SCRATCH_BASE + 0x60, 0x00000000u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "move.l #3,%%d5\n\t"
        "move.b 0x01ff9150(%%d4:l),0x01ff9160(%%d5:l)"
        :
        :
        : "d4", "d5", "memory");
    chk32(0x00070043u, rd32(SCRATCH_BASE + 0x60), 0x00000033u);

    wr32(FULLFMT_TEST_BASE + 0x108u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #4,%%d4\n\t"
        "move.b #0xb6,0x0100(%%a0,%%d4:l:2)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00070108u, rd32(FULLFMT_TEST_BASE + 0x108u), 0xb6000000u);

    wr32(FULLFMT_TEST_BASE + 0x10cu, 0u);
    __asm__ volatile(
        "lea 0x02019f00,%%a0\n\t"
        "move.l #6,%%d4\n\t"
        "move.b #0xb7,-0x1ff00(%%a0,%%d4:l:2)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x0007010cu, rd32(FULLFMT_TEST_BASE + 0x10cu), 0xb7000000u);

    wr32(FULLFMT_TEST_BASE + 0x18u, FULLFMT_TEST_BASE + 0x80u);
    wr32(FULLFMT_TEST_BASE + 0x80u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #4,%%d4\n\t"
        "move.b #0xb1,([0x10,%%a0,%%d4:l:2],0x03)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00070180u, rd32(FULLFMT_TEST_BASE + 0x80u), 0x000000b1u);

    wr32(FULLFMT_TEST_BASE + 0x20u, FULLFMT_TEST_BASE + 0x90u);
    wr32(FULLFMT_TEST_BASE + 0x98u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #2,%%d4\n\t"
        "move.b #0xb2,([0x20,%%a0],%%d4:l:2,0x04)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00070198u, rd32(FULLFMT_TEST_BASE + 0x98u), 0xb2000000u);

    wr32(FULLFMT_TEST_BASE + 0x24u, FULLFMT_TEST_BASE + 0xa0u);
    wr32(FULLFMT_TEST_BASE + 0xa4u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.b #0xb3,([0x24,%%a0],0x04)"
        :
        :
        : "a0", "memory");
    chk32(0x000701a4u, rd32(FULLFMT_TEST_BASE + 0xa4u), 0xb3000000u);

    wr32(FULLFMT_TEST_BASE + 0x2cu, FULLFMT_TEST_BASE + 0xb4u);
    wr32(FULLFMT_TEST_BASE + 0xb4u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #4,%%d4\n\t"
        "move.b #0xb4,([0x24,%%a0,%%d4:l:2])"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701b4u, rd32(FULLFMT_TEST_BASE + 0xb4u), 0xb4000000u);

    wr32(FULLFMT_TEST_BASE + 0x64u, FULLFMT_TEST_BASE + 0xc0u);
    wr32(FULLFMT_TEST_BASE + 0xc0u, 0u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "move.b #0xb5,([0x01ff9f60,%%d4:l:2],0x01)"
        :
        :
        : "d4", "memory");
    chk32(0x000701c0u, rd32(FULLFMT_TEST_BASE + 0xc0u), 0x00b50000u);

    wr32(FULLFMT_TEST_BASE + 0x60u, FULLFMT_TEST_BASE + 0xd0u);
    wr32(FULLFMT_TEST_BASE + 0xd4u, 0u);
    __asm__ volatile(
        "move.l #2,%%d4\n\t"
        "move.b #0xb8,([0x01ff9f60],%%d4:l:2,0x01)"
        :
        :
        : "d4", "memory");
    chk32(0x000701d4u, rd32(FULLFMT_TEST_BASE + 0xd4u), 0x00b80000u);
}

static void test_indexed_ea_scale_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;

    __asm__ volatile(
        "lea 0x01ffa000,%%a0\n\t"
        "moveq #3,%%d4\n\t"
        "lea 0x10(%%a0,%%d4:l:8),%%a1\n\t"
        "move.l %%a1,%0\n\t"
        "lea 0x10(%%a0,%%d4:l),%%a1\n\t"
        "move.l %%a1,%1\n\t"
        "lea 0x10(%%a0,%%d4:l:2),%%a1\n\t"
        "move.l %%a1,%2\n\t"
        "lea 0x10(%%a0,%%d4:l:4),%%a1\n\t"
        "move.l %%a1,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "a1", "d4");
    chk32(0x00070200u, got0, 0x01ffa028u);
    chk32(0x00070204u, got1, 0x01ffa013u);
    chk32(0x00070208u, got2, 0x01ffa016u);
    chk32(0x0007020cu, got3, 0x01ffa01cu);

    __asm__ volatile(
        "lea 0x01ffa100,%%a0\n\t"
        "movea.l #5,%%a2\n\t"
        "move.l #-2,%%d4\n\t"
        "lea 0x20(%%a0,%%a2:l:4),%%a1\n\t"
        "move.l %%a1,%0\n\t"
        "lea 0x30(%%a0,%%d4:w:8),%%a1\n\t"
        "move.l %%a1,%1\n\t"
        "move.l #0x00010002,%%d4\n\t"
        "lea 0x30(%%a0,%%d4:w:4),%%a1\n\t"
        "move.l %%a1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "a2", "d4");
    chk32(0x00070210u, got0, 0x01ffa134u);
    chk32(0x00070214u, got1, 0x01ffa120u);
    chk32(0x00070218u, got2, 0x01ffa138u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff96e0,%%sp\n\t"
        "lea 0x01ffa200,%%a0\n\t"
        "moveq #3,%%d4\n\t"
        "pea 0x10(%%a0,%%d4:l:8)\n\t"
        "move.l (%%sp),%0\n\t"
        "lea 4(%%sp),%%sp\n\t"
        "pea 0x10(%%a0,%%d4:l)\n\t"
        "move.l (%%sp),%1\n\t"
        "lea 4(%%sp),%%sp\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a2", "d4", "memory");
    chk32(0x00070220u, got0, 0x01ffa228u);
    chk32(0x00070224u, got1, 0x01ffa213u);

    __asm__ volatile(
        "moveq #3,%%d4\n\t"
        "lea 1f,%%a0\n\t"
        "lea 1f-24(%%pc,%%d4:l:8),%%a1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "lea 1f-3(%%pc,%%d4:l),%%a1\n\t"
        "move.l %%a1,%2\n\t"
        "bra 2f\n"
        "1:\n\t"
        "nop\n"
        "2:"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d4", "memory");
    chk32(0x00070230u, got1, got0);
    chk32(0x00070234u, got2, got0);

    __asm__ volatile(
        "moveq #2,%%d4\n\t"
        "lea 1f,%%a0\n\t"
        "pea 1f-8(%%pc,%%d4:l:4)\n\t"
        "move.l (%%sp)+,%0\n\t"
        "lea 1f-2(%%pc,%%d4:l),%%a1\n\t"
        "move.l %%a0,%1\n\t"
        "move.l %%a1,%2\n\t"
        "bra 2f\n"
        "1:\n\t"
        "nop\n"
        "2:"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d4", "memory");
    chk32(0x00070240u, got0, got1);
    chk32(0x00070244u, got2, got1);
}

static void test_an_indexed_stores(void)
{
    wr32(SCRATCH_BASE + 0x80, 0x00000000u);
    __asm__ volatile(
        "lea 0x01ff9180,%%a0\n\t"
        "move.l #2,%%d4\n\t"
        "move.b #0x71,0(%%a0,%%d4:l)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00050002u, rd32(SCRATCH_BASE + 0x80), 0x00007100u);

    wr32(SCRATCH_BASE + 0x84, 0x00000000u);
    __asm__ volatile(
        "lea 0x01ff9180,%%a0\n\t"
        "move.b #0x72,6(%%a0)"
        :
        :
        : "a0", "memory");
    chk32(0x00050006u, rd32(SCRATCH_BASE + 0x84), 0x00007200u);
}

static void test_an_post_pre_byte(void)
{
    uint32_t after;

    wr32(SCRATCH_BASE + 0x90, 0x00000000u);
    __asm__ volatile(
        "lea 0x01ff9191,%%a0\n\t"
        "move.b #0x81,(%%a0)+\n\t"
        "move.l %%a0,%0"
        : "=d"(after)
        :
        : "a0", "memory");
    chk8(0x00060001u, rd8(SCRATCH_BASE + 0x91), 0x81u);
    chk32(0x00060011u, after, SCRATCH_BASE + 0x92);

    wr32(SCRATCH_BASE + 0x90, 0x00000000u);
    __asm__ volatile(
        "lea 0x01ff9193,%%a0\n\t"
        "move.b #0x82,-(%%a0)\n\t"
        "move.l %%a0,%0"
        : "=d"(after)
        :
        : "a0", "memory");
    chk8(0x00060002u, rd8(SCRATCH_BASE + 0x92), 0x82u);
    chk32(0x00060012u, after, SCRATCH_BASE + 0x92);
}

static void test_movem_directed(void)
{
    wr32(SCRATCH_BASE + 0xa0, 0u);
    wr32(SCRATCH_BASE + 0xa4, 0u);
    wr32(SCRATCH_BASE + 0xa8, 0u);
    wr32(SCRATCH_BASE + 0xac, 0u);
    __asm__ volatile(
        "lea 0x01ff91a0,%%a2\n\t"
        "move.l #0x11112222,%%d0\n\t"
        "move.l #0x33334444,%%d1\n\t"
        "move.l #0x55556666,%%d2\n\t"
        "move.l #0x77778888,%%d3\n\t"
        "movem.l %%d0-%%d3,(%%a2)"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x00080000u, rd32(SCRATCH_BASE + 0xa0), 0x11112222u);
    chk32(0x00080004u, rd32(SCRATCH_BASE + 0xa4), 0x33334444u);
    chk32(0x00080008u, rd32(SCRATCH_BASE + 0xa8), 0x55556666u);
    chk32(0x0008000cu, rd32(SCRATCH_BASE + 0xac), 0x77778888u);

    wr32(SCRATCH_BASE + 0xb0, 0x89abcdefu);
    wr32(SCRATCH_BASE + 0xb4, 0x01234567u);
    wr32(SCRATCH_BASE + 0xb8, 0x0badc0deu);
    wr32(SCRATCH_BASE + 0xbc, 0xfeedfaceu);
    wr32(SCRATCH_BASE + 0xc0, 0u);
    wr32(SCRATCH_BASE + 0xc4, 0u);
    wr32(SCRATCH_BASE + 0xc8, 0u);
    wr32(SCRATCH_BASE + 0xcc, 0u);
    __asm__ volatile(
        "lea 0x01ff91b0,%%a2\n\t"
        "lea 0x01ff91c0,%%a3\n\t"
        "movem.l (%%a2),%%d0-%%d3\n\t"
        "movem.l %%d0-%%d3,(%%a3)"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "a3", "memory");
    chk32(0x00080010u, rd32(SCRATCH_BASE + 0xc0), 0x89abcdefu);
    chk32(0x00080014u, rd32(SCRATCH_BASE + 0xc4), 0x01234567u);
    chk32(0x00080018u, rd32(SCRATCH_BASE + 0xc8), 0x0badc0deu);
    chk32(0x0008001cu, rd32(SCRATCH_BASE + 0xcc), 0xfeedfaceu);

    wr32(SCRATCH_BASE + 0xd0, 0x10203040u);
    wr32(SCRATCH_BASE + 0xd4, 0x50607080u);
    wr32(SCRATCH_BASE + 0xd8, 0x90a0b0c0u);
    wr32(SCRATCH_BASE + 0xdc, 0xd0e0f001u);
    wr32(SCRATCH_BASE + 0xfc, 0u);
    __asm__ volatile(
        "lea 0x01ff91d0,%%a2\n\t"
        "movem.l (%%a2)+,%%d0-%%d3\n\t"
        "move.l %%a2,0x01ff91fc"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x00080020u, rd32(SCRATCH_BASE + 0xfc), SCRATCH_BASE + 0xe0);

    wr32(SCRATCH_BASE + 0xa0, 0u);
    wr32(SCRATCH_BASE + 0xa4, 0u);
    wr32(SCRATCH_BASE + 0xa8, 0u);
    wr32(SCRATCH_BASE + 0xac, 0u);
    wr32(SCRATCH_BASE + 0xfc, 0u);
    __asm__ volatile(
        "lea 0x01ff91b0,%%a2\n\t"
        "move.l #0x01020304,%%d0\n\t"
        "move.l #0x11121314,%%d1\n\t"
        "move.l #0x21222324,%%d2\n\t"
        "move.l #0x31323334,%%d3\n\t"
        "movem.l %%d0-%%d3,-(%%a2)\n\t"
        "move.l %%a2,0x01ff91fc"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x00080030u, rd32(SCRATCH_BASE + 0xfc), SCRATCH_BASE + 0xa0);
    chk32(0x00080034u, rd32(SCRATCH_BASE + 0xa0), 0x01020304u);
    chk32(0x00080038u, rd32(SCRATCH_BASE + 0xa4), 0x11121314u);
    chk32(0x0008003cu, rd32(SCRATCH_BASE + 0xa8), 0x21222324u);
    chk32(0x00080040u, rd32(SCRATCH_BASE + 0xac), 0x31323334u);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    wr32(SCRATCH_BASE + 0xe8, 0u);
    __asm__ volatile(
        "move.l #0x13579bdf,%%d2\n\t"
        "move.l #0x2468ace0,%%d3\n\t"
        "move.l #0x0f1e2d3c,%%d4\n\t"
        "movem.l %%d2-%%d4,-(%%sp)\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "moveq #0,%%d4\n\t"
        "movem.l (%%sp)+,%%d2-%%d4\n\t"
        "move.l %%d2,0x01ff91e0\n\t"
        "move.l %%d3,0x01ff91e4\n\t"
        "move.l %%d4,0x01ff91e8"
        :
        :
        : "d2", "d3", "d4", "memory");
    chk32(0x00080070u, rd32(SCRATCH_BASE + 0xe0), 0x13579bdfu);
    chk32(0x00080074u, rd32(SCRATCH_BASE + 0xe4), 0x2468ace0u);
    chk32(0x00080078u, rd32(SCRATCH_BASE + 0xe8), 0x0f1e2d3cu);

    for (uint32_t off = 0x100u; off < 0x130u; off += 4u) {
        wr32(SCRATCH_BASE + off, 0u);
    }
    __asm__ volatile(
        "lea 0x01ff9220,%%a2\n\t"
        "move.l #0x01010101,%%d0\n\t"
        "move.l #0x02020202,%%d1\n\t"
        "move.l #0x03030303,%%d2\n\t"
        "move.l #0x04040404,%%d3\n\t"
        "movem.l %%d0-%%d1,-(%%a2)\n\t"
        "movem.l %%d2-%%d3,-(%%a2)\n\t"
        "move.l %%a2,0x01ff9220"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "memory");
    chk32(0x00080080u, rd32(SCRATCH_BASE + 0x110), 0x03030303u);
    chk32(0x00080084u, rd32(SCRATCH_BASE + 0x114), 0x04040404u);
    chk32(0x00080088u, rd32(SCRATCH_BASE + 0x118), 0x01010101u);
    chk32(0x0008008cu, rd32(SCRATCH_BASE + 0x11c), 0x02020202u);
    chk32(0x00080090u, rd32(SCRATCH_BASE + 0x120), SCRATCH_BASE + 0x110u);

    wr32(SCRATCH_BASE + 0xb0, 0u);
    wr32(SCRATCH_BASE + 0xb4, 0u);
    wr32(SCRATCH_BASE + 0xb8, 0u);
    wr32(SCRATCH_BASE + 0xbc, 0u);
    __asm__ volatile(
        "move.l #0x10,%%d4\n\t"
        "move.l #0xa1a2a3a4,%%d0\n\t"
        "move.l #0xb1b2b3b4,%%d1\n\t"
        "move.l #0xc1c2c3c4,%%d2\n\t"
        "move.l #0xd1d2d3d4,%%d3\n\t"
        "movem.l %%d0-%%d3,0x01ff91a0(%%d4:l)"
        :
        :
        : "d0", "d1", "d2", "d3", "d4", "memory");
    chk32(0x00080050u, rd32(SCRATCH_BASE + 0xb0), 0xa1a2a3a4u);
    chk32(0x00080054u, rd32(SCRATCH_BASE + 0xb4), 0xb1b2b3b4u);
    chk32(0x00080058u, rd32(SCRATCH_BASE + 0xb8), 0xc1c2c3c4u);
    chk32(0x0008005cu, rd32(SCRATCH_BASE + 0xbc), 0xd1d2d3d4u);

    wr32(SCRATCH_BASE + 0xd0, 0u);
    wr32(SCRATCH_BASE + 0xd4, 0u);
    wr32(SCRATCH_BASE + 0xd8, 0u);
    wr32(SCRATCH_BASE + 0xdc, 0u);
    __asm__ volatile(
        "move.l #0x10,%%d4\n\t"
        "lea 0x01ff91d0,%%a3\n\t"
        "movem.l 0x01ff91a0(%%d4:l),%%d0-%%d3\n\t"
        "movem.l %%d0-%%d3,(%%a3)"
        :
        :
        : "d0", "d1", "d2", "d3", "d4", "a3", "memory");
    chk32(0x00080060u, rd32(SCRATCH_BASE + 0xd0), 0xa1a2a3a4u);
    chk32(0x00080064u, rd32(SCRATCH_BASE + 0xd4), 0xb1b2b3b4u);
    chk32(0x00080068u, rd32(SCRATCH_BASE + 0xd8), 0xc1c2c3c4u);
    chk32(0x0008006cu, rd32(SCRATCH_BASE + 0xdc), 0xd1d2d3d4u);

    wr32(SCRATCH_BASE + 0x130, 0u);
    wr32(SCRATCH_BASE + 0x134, 0u);
    __asm__ volatile(
        "lea 0x01ff9230,%%a2\n\t"
        "move.l #0xaaaa1111,%%d0\n\t"
        "move.l #0xbbbb2222,%%d1\n\t"
        "move.l #0xcccc3333,%%d2\n\t"
        "move.l #0xdddd4444,%%d3\n\t"
        "movem.w %%d0-%%d3,(%%a2)"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x00080094u, rd32(SCRATCH_BASE + 0x130), 0x11112222u);
    chk32(0x00080098u, rd32(SCRATCH_BASE + 0x134), 0x33334444u);

    wr32(SCRATCH_BASE + 0x138, 0x7fff8000u);
    wr32(SCRATCH_BASE + 0x13c, 0x0001ffffu);
    wr32(SCRATCH_BASE + 0x140, 0u);
    wr32(SCRATCH_BASE + 0x144, 0u);
    wr32(SCRATCH_BASE + 0x148, 0u);
    wr32(SCRATCH_BASE + 0x14c, 0u);
    __asm__ volatile(
        "lea 0x01ff9238,%%a2\n\t"
        "move.l #0xaaaaaaaa,%%d0\n\t"
        "move.l #0xbbbbbbbb,%%d1\n\t"
        "move.l #0xcccccccc,%%d2\n\t"
        "move.l #0xdddddddd,%%d3\n\t"
        "movem.w (%%a2),%%d0-%%d3\n\t"
        "move.l %%d0,0x01ff9240\n\t"
        "move.l %%d1,0x01ff9244\n\t"
        "move.l %%d2,0x01ff9248\n\t"
        "move.l %%d3,0x01ff924c"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x000800a0u, rd32(SCRATCH_BASE + 0x140), 0x00007fffu);
    chk32(0x000800a4u, rd32(SCRATCH_BASE + 0x144), 0xffff8000u);
    chk32(0x000800a8u, rd32(SCRATCH_BASE + 0x148), 0x00000001u);
    chk32(0x000800acu, rd32(SCRATCH_BASE + 0x14c), 0xffffffffu);

    wr32(SCRATCH_BASE + 0x140, 0u);
    wr32(SCRATCH_BASE + 0x144, 0u);
    wr32(SCRATCH_BASE + 0x148, 0u);
    wr32(SCRATCH_BASE + 0x14c, 0u);
    wr32(SCRATCH_BASE + 0x150, 0x7fff8000u);
    wr32(SCRATCH_BASE + 0x154, 0x0001ffffu);
    __asm__ volatile(
        "lea 0x01ff9250,%%a4\n\t"
        "movem.w (%%a4),%%a0-%%a3\n\t"
        "move.l %%a0,0x01ff9240\n\t"
        "move.l %%a1,0x01ff9244\n\t"
        "move.l %%a2,0x01ff9248\n\t"
        "move.l %%a3,0x01ff924c"
        :
        :
        : "a0", "a1", "a2", "a3", "a4", "memory");
    chk32(0x000800c0u, rd32(SCRATCH_BASE + 0x140), 0x00007fffu);
    chk32(0x000800c4u, rd32(SCRATCH_BASE + 0x144), 0xffff8000u);
    chk32(0x000800c8u, rd32(SCRATCH_BASE + 0x148), 0x00000001u);
    chk32(0x000800ccu, rd32(SCRATCH_BASE + 0x14c), 0xffffffffu);

    wr32(SCRATCH_BASE + 0x160, 0u);
    wr32(SCRATCH_BASE + 0x164, 0u);
    wr32(SCRATCH_BASE + 0x168, 0u);
    wr32(SCRATCH_BASE + 0x16c, 0u);
    __asm__ volatile(
        "lea 0x01ff9268,%%a2\n\t"
        "move.l #0xaaaa1357,%%d0\n\t"
        "move.l #0xbbbb2468,%%d1\n\t"
        "move.l #0xcccc369a,%%d2\n\t"
        "move.l #0xdddd48bc,%%d3\n\t"
        "movem.w %%d0-%%d3,-(%%a2)\n\t"
        "move.l %%a2,0x01ff926c"
        :
        :
        : "d0", "d1", "d2", "d3", "a2", "memory");
    chk32(0x000800b0u, rd32(SCRATCH_BASE + 0x160), 0x13572468u);
    chk32(0x000800b4u, rd32(SCRATCH_BASE + 0x164), 0x369a48bcu);
    chk32(0x000800b8u, rd32(SCRATCH_BASE + 0x16c), SCRATCH_BASE + 0x160u);

    wr32(SCRATCH_BASE + 0x150, 0u);
    wr32(SCRATCH_BASE + 0x154, 0u);
    wr32(SCRATCH_BASE + 0x158, 0u);
    wr32(SCRATCH_BASE + 0x15c, 0u);
    __asm__ volatile(
        "lea 0x01ff9250,%%a2\n\t"
        "move.l #0x10203040,%%d0\n\t"
        "move.l #0x50607080,%%d1\n\t"
        "movea.l #0x90a0b0c0,%%a0\n\t"
        "movea.l #0xd0e0f001,%%a1\n\t"
        "movem.l %%d0-%%d1/%%a0-%%a1,(%%a2)"
        :
        :
        : "d0", "d1", "a0", "a1", "a2", "memory");
    chk32(0x000800d0u, rd32(SCRATCH_BASE + 0x150), 0x10203040u);
    chk32(0x000800d4u, rd32(SCRATCH_BASE + 0x154), 0x50607080u);
    chk32(0x000800d8u, rd32(SCRATCH_BASE + 0x158), 0x90a0b0c0u);
    chk32(0x000800dcu, rd32(SCRATCH_BASE + 0x15c), 0xd0e0f001u);

    wr32(SCRATCH_BASE + 0x150, 0u);
    wr32(SCRATCH_BASE + 0x154, 0u);
    wr32(SCRATCH_BASE + 0x158, 0u);
    wr32(SCRATCH_BASE + 0x15c, 0u);
    wr32(SCRATCH_BASE + 0x160, 0u);
    __asm__ volatile(
        "lea 0x01ff9260,%%a2\n\t"
        "move.l #0x10203040,%%d0\n\t"
        "move.l #0x50607080,%%d1\n\t"
        "movea.l #0x90a0b0c0,%%a0\n\t"
        "movea.l #0xd0e0f001,%%a1\n\t"
        "movem.l %%d0-%%d1/%%a0-%%a1,-(%%a2)\n\t"
        "move.l %%a2,0x01ff9260"
        :
        :
        : "d0", "d1", "a0", "a1", "a2", "memory");
    chk32(0x000800e0u, rd32(SCRATCH_BASE + 0x150), 0x10203040u);
    chk32(0x000800e4u, rd32(SCRATCH_BASE + 0x154), 0x50607080u);
    chk32(0x000800e8u, rd32(SCRATCH_BASE + 0x158), 0x90a0b0c0u);
    chk32(0x000800ecu, rd32(SCRATCH_BASE + 0x15c), 0xd0e0f001u);
    chk32(0x000800f0u, rd32(SCRATCH_BASE + 0x160), SCRATCH_BASE + 0x150u);

    wr32(SCRATCH_BASE + 0x1c0, 0x13572468u);
    wr32(SCRATCH_BASE + 0x1c4, 0x24681357u);
    wr32(SCRATCH_BASE + 0x1c8, 0x3579ace0u);
    wr32(SCRATCH_BASE + 0x1cc, 0x468ace02u);
    wr32(SCRATCH_BASE + 0x1d0, 0u);
    wr32(SCRATCH_BASE + 0x1d4, 0u);
    wr32(SCRATCH_BASE + 0x1d8, 0u);
    wr32(SCRATCH_BASE + 0x1dc, 0u);
    __asm__ volatile(
        "lea 0x01ff92c0,%%a5\n\t"
        "movem.l (%%a5),%%d0-%%d1/%%a0-%%a1\n\t"
        "move.l %%d0,0x01ff92d0\n\t"
        "move.l %%d1,0x01ff92d4\n\t"
        "move.l %%a0,0x01ff92d8\n\t"
        "move.l %%a1,0x01ff92dc"
        :
        :
        : "d0", "d1", "a0", "a1", "a5", "memory");
    chk32(0x00080100u, rd32(SCRATCH_BASE + 0x1d0), 0x13572468u);
    chk32(0x00080104u, rd32(SCRATCH_BASE + 0x1d4), 0x24681357u);
    chk32(0x00080108u, rd32(SCRATCH_BASE + 0x1d8), 0x3579ace0u);
    chk32(0x0008010cu, rd32(SCRATCH_BASE + 0x1dc), 0x468ace02u);

    wr32(SCRATCH_BASE + 0x200, 0x01020304u);
    wr32(SCRATCH_BASE + 0x204, 0x11121314u);
    wr32(SCRATCH_BASE + 0x208, 0x21222324u);
    wr32(SCRATCH_BASE + 0x20c, 0x31323334u);
    wr32(SCRATCH_BASE + 0x210, 0u);
    __asm__ volatile(
        "lea 0x01ff9300,%%a5\n\t"
        "move.w #0x001f,%%ccr\n\t"
        "movem.l (%%a5),%%d0-%%d1/%%a0-%%a1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff9310"
        :
        :
        : "d0", "d1", "d2", "a0", "a1", "a5", "cc", "memory");
    chk32(0x00080110u, rd32(SCRATCH_BASE + 0x210) & 0x1fu, 0x1fu);

    wr32(SCRATCH_BASE + 0x220, 0u);
    wr32(SCRATCH_BASE + 0x224, 0u);
    wr32(SCRATCH_BASE + 0x228, 0u);
    wr32(SCRATCH_BASE + 0x22c, 0u);
    wr32(SCRATCH_BASE + 0x230, 0u);
    __asm__ volatile(
        "lea 0x01ff9320,%%a5\n\t"
        "move.l #0x01020304,%%d0\n\t"
        "move.l #0x11121314,%%d1\n\t"
        "movea.l #0x21222324,%%a0\n\t"
        "movea.l #0x31323334,%%a1\n\t"
        "move.w #0x0015,%%ccr\n\t"
        "movem.l %%d0-%%d1/%%a0-%%a1,(%%a5)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff9330"
        :
        :
        : "d0", "d1", "d2", "a0", "a1", "a5", "cc", "memory");
    chk32(0x00080114u, rd32(SCRATCH_BASE + 0x220), 0x01020304u);
    chk32(0x00080118u, rd32(SCRATCH_BASE + 0x224), 0x11121314u);
    chk32(0x0008011cu, rd32(SCRATCH_BASE + 0x228), 0x21222324u);
    chk32(0x00080120u, rd32(SCRATCH_BASE + 0x22c), 0x31323334u);
    chk32(0x00080124u, rd32(SCRATCH_BASE + 0x230) & 0x1fu, 0x15u);

    wr32(SCRATCH_BASE + 0x240, 0x01020304u);
    wr32(SCRATCH_BASE + 0x244, 0x11121314u);
    wr32(SCRATCH_BASE + 0x248, 0x21222324u);
    wr32(SCRATCH_BASE + 0x24c, 0x31323334u);
    wr32(SCRATCH_BASE + 0x250, 0x41424344u);
    wr32(SCRATCH_BASE + 0x254, 0x51525354u);
    wr32(SCRATCH_BASE + 0x258, 0x61626364u);
    wr32(SCRATCH_BASE + 0x25c, 0x71727374u);
    wr32(SCRATCH_BASE + 0x260, 0x81828384u);
    wr32(SCRATCH_BASE + 0x264, 0x91929394u);
    wr32(SCRATCH_BASE + 0x268, 0xa1a2a3a4u);
    wr32(SCRATCH_BASE + 0x26c, 0xb1b2b3b4u);
    wr32(SCRATCH_BASE + 0x270, 0xc1c2c3c4u);
    wr32(SCRATCH_BASE + 0x274, 0xd1d2d3d4u);
    wr32(SCRATCH_BASE + 0x278, 0xe1e2e3e4u);
    for (uint32_t off = 0x280u; off < 0x2c0u; off += 4u) {
        wr32(SCRATCH_BASE + off, 0u);
    }
    coretest_movem_fullshape_asm();
    chk32(0x00080130u, rd32(SCRATCH_BASE + 0x280), 0x01020304u);
    chk32(0x00080134u, rd32(SCRATCH_BASE + 0x284), 0x11121314u);
    chk32(0x00080138u, rd32(SCRATCH_BASE + 0x288), 0x21222324u);
    chk32(0x0008013cu, rd32(SCRATCH_BASE + 0x28c), 0x31323334u);
    chk32(0x00080140u, rd32(SCRATCH_BASE + 0x290), 0x41424344u);
    chk32(0x00080144u, rd32(SCRATCH_BASE + 0x294), 0x51525354u);
    chk32(0x00080148u, rd32(SCRATCH_BASE + 0x298), 0x61626364u);
    chk32(0x0008014cu, rd32(SCRATCH_BASE + 0x29c), 0x71727374u);
    chk32(0x00080150u, rd32(SCRATCH_BASE + 0x2a0), 0x81828384u);
    chk32(0x00080154u, rd32(SCRATCH_BASE + 0x2a4), 0x91929394u);
    chk32(0x00080158u, rd32(SCRATCH_BASE + 0x2a8), 0xa1a2a3a4u);
    chk32(0x0008015cu, rd32(SCRATCH_BASE + 0x2ac), 0xb1b2b3b4u);
    chk32(0x00080160u, rd32(SCRATCH_BASE + 0x2b0), 0xc1c2c3c4u);
    chk32(0x00080164u, rd32(SCRATCH_BASE + 0x2b4), 0xd1d2d3d4u);
    chk32(0x00080168u, rd32(SCRATCH_BASE + 0x2b8), 0xe1e2e3e4u);
    chk32(0x0008016cu, rd32(SCRATCH_BASE + 0x2bc) & 0x1fu, 0x1bu);

    for (uint32_t off = 0x280u; off < 0x2c0u; off += 4u) {
        wr32(SCRATCH_BASE + off, 0u);
    }
    *(volatile uint16_t *)(SCRATCH_BASE + 0x2c0) = 0x4e75u;
    *(volatile uint16_t *)(SCRATCH_BASE + 0x2c2) = 0x4e71u;
    coretest_movem_ramjsr_asm();
    chk32(0x00080170u, rd32(SCRATCH_BASE + 0x280), 0x01020304u);
    chk32(0x00080174u, rd32(SCRATCH_BASE + 0x284), 0x11121314u);
    chk32(0x00080178u, rd32(SCRATCH_BASE + 0x288), 0x21222324u);
    chk32(0x0008017cu, rd32(SCRATCH_BASE + 0x28c), 0x31323334u);
    chk32(0x00080180u, rd32(SCRATCH_BASE + 0x290), 0x41424344u);
    chk32(0x00080184u, rd32(SCRATCH_BASE + 0x294), 0x51525354u);
    chk32(0x00080188u, rd32(SCRATCH_BASE + 0x298), 0x61626364u);
    chk32(0x0008018cu, rd32(SCRATCH_BASE + 0x29c), 0x71727374u);
    chk32(0x00080190u, rd32(SCRATCH_BASE + 0x2a0), 0x81828384u);
    chk32(0x00080194u, rd32(SCRATCH_BASE + 0x2a4), 0x91929394u);
    chk32(0x00080198u, rd32(SCRATCH_BASE + 0x2a8), 0xa1a2a3a4u);
    chk32(0x0008019cu, rd32(SCRATCH_BASE + 0x2ac), 0xb1b2b3b4u);
    chk32(0x000801a0u, rd32(SCRATCH_BASE + 0x2b0), 0xc1c2c3c4u);
    chk32(0x000801a4u, rd32(SCRATCH_BASE + 0x2b4), 0xd1d2d3d4u);
    chk32(0x000801a8u, rd32(SCRATCH_BASE + 0x2b8), 0xe1e2e3e4u);
    chk32(0x000801acu, rd32(SCRATCH_BASE + 0x2bc) & 0x1fu, 0x1du);

    for (uint32_t off = 0x280u; off < 0x2c0u; off += 4u) {
        wr32(SCRATCH_BASE + off, 0u);
    }
    *(volatile uint16_t *)(SCRATCH_BASE + 0x2c0) = 0xd041u;
    *(volatile uint16_t *)(SCRATCH_BASE + 0x2c2) = 0x4e75u;
    coretest_movem_ramadd_asm();
    chk32(0x000801b0u, rd32(SCRATCH_BASE + 0x280), 0x01021618u);
    chk32(0x000801b4u, rd32(SCRATCH_BASE + 0x284), 0x11121314u);
    chk32(0x000801b8u, rd32(SCRATCH_BASE + 0x288), 0x21222324u);
    chk32(0x000801bcu, rd32(SCRATCH_BASE + 0x28c), 0x31323334u);
    chk32(0x000801c0u, rd32(SCRATCH_BASE + 0x290), 0x41424344u);
    chk32(0x000801c4u, rd32(SCRATCH_BASE + 0x294), 0x51525354u);
    chk32(0x000801c8u, rd32(SCRATCH_BASE + 0x298), 0x61626364u);
    chk32(0x000801ccu, rd32(SCRATCH_BASE + 0x29c), 0x71727374u);
    chk32(0x000801d0u, rd32(SCRATCH_BASE + 0x2a0), 0x81828384u);
    chk32(0x000801d4u, rd32(SCRATCH_BASE + 0x2a4), 0x91929394u);
    chk32(0x000801d8u, rd32(SCRATCH_BASE + 0x2a8), 0xa1a2a3a4u);
    chk32(0x000801dcu, rd32(SCRATCH_BASE + 0x2ac), 0xb1b2b3b4u);
    chk32(0x000801e0u, rd32(SCRATCH_BASE + 0x2b0), 0xc1c2c3c4u);
    chk32(0x000801e4u, rd32(SCRATCH_BASE + 0x2b4), 0xd1d2d3d4u);
    chk32(0x000801e8u, rd32(SCRATCH_BASE + 0x2b8), 0xe1e2e3e4u);
    chk32(0x000801ecu, rd32(SCRATCH_BASE + 0x2bc) & 0x1fu, 0x00u);
}

static void test_control_flow_directed(void)
{
    uint32_t got;
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;
    uint32_t got4;

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #5,%%d1\n\t"
        "cmp.l #5,%%d1\n\t"
        "beq 1f\n\t"
        "moveq #1,%%d0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #2,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x00090000u, got, 2u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #5,%%d1\n\t"
        "cmp.l #5,%%d1\n\t"
        "bne 1f\n\t"
        "moveq #3,%%d0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #4,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x00090001u, got, 3u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #9,%%d1\n"
        "1:\n\t"
        "addq.l #1,%%d0\n\t"
        "dbra %%d1,1b\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x00090010u, got, 10u);

    __asm__ volatile(
        "moveq #3,%%d1\n\t"
        "moveq #0,%%d0\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "dbeq %%d1,1f\n\t"
        "move.l %%d1,%%d0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #0x7f,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x00090011u, got, 3u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "lea 1f,%%a0\n\t"
        "jsr (%%a0)\n\t"
        "bra 2f\n"
        "1:\n\t"
        "move.l #0x13579bdf,%%d0\n\t"
        "rts\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "a0", "memory");
    chk32(0x00090020u, got, 0x13579bdfu);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "bsr 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "move.l #0x2468ace0,%%d0\n\t"
        "rts\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "memory");
    chk32(0x00090021u, got, 0x2468ace0u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 2f,%%a1\n\t"
        "move.l %%a1,%4\n\t"
        "lea 1f-16,%%a0\n\t"
        "jsr 16(%%a0)\n"
        "2:\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%sp,%2\n\t"
        "move.l %%a2,%3\n\t"
        "bra 3f\n"
        "1:\n\t"
        "move.l (%%sp),%%d1\n\t"
        "move.l #0x55aa0101,%%d0\n\t"
        "rts\n"
        "3:"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3), "=&d"(got4)
        :
        : "a0", "a1", "a2", "d0", "d1", "memory");
    chk32(0x00090024u, got0, 0x55aa0101u);
    chk32(0x00090028u, got1, got4);
    chk32(0x0009002cu, got2, got3);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 2f,%%a1\n\t"
        "move.l %%a1,%4\n\t"
        "lea 1f-20,%%a0\n\t"
        "moveq #20,%%d2\n\t"
        "jsr 0(%%a0,%%d2:l)\n"
        "2:\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%sp,%2\n\t"
        "move.l %%a2,%3\n\t"
        "bra 3f\n"
        "1:\n\t"
        "move.l (%%sp),%%d1\n\t"
        "move.l #0x55aa0202,%%d0\n\t"
        "rts\n"
        "3:"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3), "=&d"(got4)
        :
        : "a0", "a1", "a2", "d0", "d1", "d2", "memory");
    chk32(0x00090034u, got0, 0x55aa0202u);
    chk32(0x00090038u, got1, got4);
    chk32(0x0009003cu, got2, got3);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "lea 1f,%%a0\n\t"
        "jmp (%%a0)\n\t"
        "moveq #1,%%d0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #0x2a,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "a0", "memory");
    chk32(0x00090030u, got, 0x2au);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "lea 1f-12,%%a0\n\t"
        "moveq #12,%%d2\n\t"
        "jmp 0(%%a0,%%d2:l)\n\t"
        "moveq #1,%%d0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #0x3b,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "a0", "d0", "d2", "memory");
    chk32(0x0009004cu, got, 0x3bu);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "bra.l 1f\n\t"
        "moveq #1,%%d0\n"
        "1:\n\t"
        "moveq #0x31,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "cc", "memory");
    chk32(0x00090040u, got, 0x31u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #7,%%d1\n\t"
        "cmp.l #7,%%d1\n\t"
        "beq.l 1f\n\t"
        "moveq #1,%%d0\n\t"
        "bra.s 2f\n"
        "1:\n\t"
        "moveq #0x32,%%d0\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x00090044u, got, 0x32u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "bsr.l 1f\n\t"
        "bra.s 2f\n"
        "1:\n\t"
        "move.l #0x13572468,%%d0\n\t"
        "rts\n"
        "2:\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "memory");
    chk32(0x00090048u, got, 0x13572468u);
}

static void test_stack_frame_control_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;
    uint32_t got4;

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff9700,%%sp\n\t"
        "pea (%%sp)\n\t"
        "move.l (%%sp),%0\n\t"
        "move.l %%sp,%1\n\t"
        "lea 4(%%sp),%%sp\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a2", "memory");
    chk32(0x00091000u, got0, STACK_TEST_BASE);
    chk32(0x00091004u, got1, STACK_TEST_BASE - 4u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff97d0,%%sp\n\t"
        "lea 0x01ff9800,%%a0\n\t"
        "pea 0x34(%%a0)\n\t"
        "move.l (%%sp),%0\n\t"
        "move.l %%sp,%1\n\t"
        "lea 4(%%sp),%%sp\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a2", "memory");
    chk32(0x00091060u, got0, 0x01ff9834u);
    chk32(0x00091064u, got1, STACK_TEST_BASE + 0xccu);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff97c0,%%sp\n\t"
        "lea 0x01ff9800,%%a0\n\t"
        "moveq #5,%%d4\n\t"
        "pea 0x20(%%a0,%%d4:l:2)\n\t"
        "move.l (%%sp),%0\n\t"
        "move.l %%sp,%1\n\t"
        "lea 4(%%sp),%%sp\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a2", "d4", "memory");
    chk32(0x00091068u, got0, 0x01ff982au);
    chk32(0x0009106cu, got1, STACK_TEST_BASE + 0xbcu);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff9720,%%sp\n\t"
        "movea.l #0x13579bdf,%%a3\n\t"
        "link %%a3,#-8\n\t"
        "move.l (%%a3),%0\n\t"
        "move.l %%a3,%1\n\t"
        "move.l %%sp,%2\n\t"
        "unlk %%a3\n\t"
        "move.l %%a3,%3\n\t"
        "move.l %%sp,%4\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3), "=&d"(got4)
        :
        : "a2", "a3", "memory");
    chk32(0x00091010u, got0, 0x13579bdfu);
    chk32(0x00091014u, got1, STACK_TEST_BASE + 0x1cu);
    chk32(0x00091018u, got2, STACK_TEST_BASE + 0x14u);
    chk32(0x0009101cu, got3, 0x13579bdfu);
    chk32(0x00091020u, got4, STACK_TEST_BASE + 0x20u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff9740,%%sp\n\t"
        "link %%a7,#-4\n\t"
        "move.l 4(%%sp),%0\n\t"
        "move.l %%sp,%1\n\t"
        "move.l 4(%%sp),%%sp\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a2", "memory");
    chk32(0x00091030u, got0, STACK_TEST_BASE + 0x40u);
    chk32(0x00091034u, got1, STACK_TEST_BASE + 0x38u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff97e0,%%sp\n\t"
        "movea.l #0x2468ace0,%%a4\n\t"
        "link.l %%a4,#-32\n\t"
        "move.l (%%a4),%0\n\t"
        "move.l %%a4,%1\n\t"
        "move.l %%sp,%2\n\t"
        "unlk %%a4\n\t"
        "move.l %%a4,%3\n\t"
        "move.l %%sp,%4\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3), "=&d"(got4)
        :
        : "a2", "a4", "memory");
    chk32(0x00091070u, got0, 0x2468ace0u);
    chk32(0x00091074u, got1, STACK_TEST_BASE + 0xdcu);
    chk32(0x00091078u, got2, STACK_TEST_BASE + 0xbcu);
    chk32(0x0009107cu, got3, 0x2468ace0u);
    chk32(0x00091080u, got4, STACK_TEST_BASE + 0xe0u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff97f0,%%sp\n\t"
        "movea.l #0x01020304,%%a5\n\t"
        "link.l %%a5,#0x1234\n\t"
        "move.l (%%a5),%0\n\t"
        "move.l %%a5,%1\n\t"
        "move.l %%sp,%2\n\t"
        "unlk %%a5\n\t"
        "move.l %%a5,%3\n\t"
        "move.l %%sp,%4\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3), "=&d"(got4)
        :
        : "a2", "a5", "memory");
    chk32(0x00091090u, got0, 0x01020304u);
    chk32(0x00091094u, got1, STACK_TEST_BASE + 0xecu);
    chk32(0x00091098u, got2, STACK_TEST_BASE + 0x1320u);
    chk32(0x0009109cu, got3, 0x01020304u);
    chk32(0x000910a0u, got4, STACK_TEST_BASE + 0xf0u);

    __asm__ volatile(
        "move.l #0x11112222,%%d0\n\t"
        "move.l #0x33334444,%%d1\n\t"
        "exg %%d0,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1");
    chk32(0x00091040u, got0, 0x33334444u);
    chk32(0x00091044u, got1, 0x11112222u);

    __asm__ volatile(
        "lea 0x01ff9760,%%a0\n\t"
        "lea 0x01ff9780,%%a1\n\t"
        "exg %%a0,%%a1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1");
    chk32(0x00091048u, got0, STACK_TEST_BASE + 0x80u);
    chk32(0x0009104cu, got1, STACK_TEST_BASE + 0x60u);

    __asm__ volatile(
        "move.l #0x01ff97a0,%%d0\n\t"
        "lea 0x01ff97c0,%%a0\n\t"
        "exg %%d0,%%a0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%a0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0");
    chk32(0x00091050u, got0, STACK_TEST_BASE + 0xc0u);
    chk32(0x00091054u, got1, STACK_TEST_BASE + 0xa0u);

    __asm__ volatile(
        "move.l %%sp,%0\n\t"
        "lea 0x01ff9760,%%a0\n\t"
        "exg %%a0,%%sp\n\t"
        "move.l %%sp,%1\n\t"
        "move.l %%a0,%2\n\t"
        "move.l %%a0,%%sp"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "memory");
    chk32(0x00091058u, got1, STACK_TEST_BASE + 0x60u);
    chk32(0x0009105cu, got2, got0);
}

static void test_address_arithmetic_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "adda.w #-2,%%a0\n\t"
        "move.l %%a0,%0"
        : "=&d"(got0)
        :
        : "a0");
    chk32(0x00092000u, got0, 0x01ffa3feu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "suba.w #-4,%%a0\n\t"
        "move.l %%a0,%0"
        : "=&d"(got0)
        :
        : "a0");
    chk32(0x00092004u, got0, 0x01ffa404u);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "adda.l #8,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc");
    chk32(0x00092008u, got0, 0x01ffa408u);
    chk32(0x0009200cu, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "addq.l #1,%%a0\n\t"
        "subq.l #2,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc");
    chk32(0x00092010u, got0, 0x01ffa3ffu);
    chk32(0x00092014u, got1 & 0x1fu, 0x1fu);
}

static void test_register_transform_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    __asm__ volatile(
        "move.l #0x12340080,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "ext.w %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093000u, got0, 0x1234ff80u);
    chk32(0x00093004u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x12348000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "ext.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093008u, got0, 0xffff8000u);
    chk32(0x0009300cu, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x1234007f,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "extb.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093010u, got0, 0x0000007fu);
    chk32(0x00093014u, got1 & 0x1fu, 0x10u);

    __asm__ volatile(
        "move.l #0x12340000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "extb.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093018u, got0, 0x00000000u);
    chk32(0x0009301cu, got1 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.l #0x00008000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "swap %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093020u, got0, 0x80000000u);
    chk32(0x00093024u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "swap %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093028u, got0, 0x00000000u);
    chk32(0x0009302cu, got1 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "movea.w #0x8001,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc");
    chk32(0x00093030u, got0, 0xffff8001u);
    chk32(0x00093034u, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "movea.l #0x80000001,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc");
    chk32(0x00093038u, got0, 0x80000001u);
    chk32(0x0009303cu, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "movea.l #0x00000010,%%a0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w #0x0010,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "cc");
    chk32(0x00093040u, got0 & 0x1fu, 0x14u);

    __asm__ volatile(
        "movea.l #0x00000010,%%a0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w #0x0020,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "cc");
    chk32(0x00093044u, got0 & 0x1fu, 0x19u);

    __asm__ volatile(
        "movea.l #0x80000000,%%a0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.l #1,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "cc");
    chk32(0x00093048u, got0 & 0x1fu, 0x12u);

    __asm__ volatile(
        "movea.l #0x7fffffff,%%a0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.l #-1,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "cc");
    chk32(0x0009304cu, got0 & 0x1fu, 0x1bu);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #-1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093050u, got0, 0xffffffffu);
    chk32(0x00093054u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "moveq #0,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093058u, got0, 0x00000000u);
    chk32(0x0009305cu, got1 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #0x7f,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093060u, got0, 0x0000007fu);
    chk32(0x00093064u, got1 & 0x1fu, 0x10u);

    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "moveq #-128,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093068u, got0, 0xffffff80u);
    chk32(0x0009306cu, got1 & 0x1fu, 0x18u);
}

static void test_unary_logic_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;

    __asm__ volatile(
        "move.l #0x12345680,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "neg.b %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094000u, got0, 0x12345680u);
    chk32(0x00094004u, got1 & 0x1fu, 0x1bu);

    __asm__ volatile(
        "move.l #1,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "neg.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094008u, got0, 0xffffffffu);
    chk32(0x0009400cu, got1 & 0x1fu, 0x19u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "neg.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094010u, got0, 0x00000000u);
    chk32(0x00094014u, got1 & 0x1fu, 0x04u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "negx.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094018u, got0, 0xffffffffu);
    chk32(0x0009401cu, got1 & 0x1fu, 0x19u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0x04,%%ccr\n\t"
        "negx.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094020u, got0, 0x00000000u);
    chk32(0x00094024u, got1 & 0x1fu, 0x04u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "negx.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094028u, got0, 0x00000000u);
    chk32(0x0009402cu, got1 & 0x1fu, 0x00u);

    __asm__ volatile(
        "move.l #0x0f0f0f0f,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "not.l %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00094030u, got0, 0xf0f0f0f0u);
    chk32(0x00094034u, got1 & 0x1fu, 0x18u);

    wr32(UNARY_TEST_BASE + 0x00u, 0xffff1234u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "clr.w 0x01ffa502\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00094038u, rd32(UNARY_TEST_BASE + 0x00u), 0xffff0000u);
    chk32(0x0009403cu, got0 & 0x1fu, 0x14u);

    wr32(UNARY_TEST_BASE + 0x04u, 0x000000ffu);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "not.b 0x01ffa507\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00094040u, rd32(UNARY_TEST_BASE + 0x04u), 0x00000000u);
    chk32(0x00094044u, got0 & 0x1fu, 0x14u);

    *(volatile uint8_t *)(UNARY_TEST_BASE + 0x09u) = 0x80u;
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "tst.b 0x01ffa509\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk8(0x00094048u, rd8(UNARY_TEST_BASE + 0x09u), 0x80u);
    chk32(0x0009404cu, got0 & 0x1fu, 0x18u);

    wr32(UNARY_TEST_BASE + 0x0cu, 0x00000000u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "tst.l 0x01ffa50c\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00094050u, got0 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "ori.b #0x15,%%ccr\n\t"
        "move.w %%sr,%%d0\n\t"
        "andi.b #0x0f,%%ccr\n\t"
        "move.w %%sr,%%d1\n\t"
        "eori.b #0x1f,%%ccr\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00094054u, got0 & 0x1fu, 0x15u);
    chk32(0x00094058u, got1 & 0x1fu, 0x05u);
    chk32(0x0009405cu, got2 & 0x1fu, 0x1au);
    chk32(0x00094070u, got0 & 0xff00u, 0x2700u);
    chk32(0x00094074u, got1 & 0xff00u, 0x2700u);
    chk32(0x00094078u, got2 & 0xff00u, 0x2700u);

    __asm__ volatile(
        "move.w #0x2700,%%sr\n\t"
        "ori.w #0x0015,%%sr\n\t"
        "move.w %%sr,%%d0\n\t"
        "andi.w #0xfff0,%%sr\n\t"
        "move.w %%sr,%%d1\n\t"
        "eori.w #0x000f,%%sr\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00094060u, got0 & 0x271fu, 0x2715u);
    chk32(0x00094064u, got1 & 0x271fu, 0x2710u);
    chk32(0x00094068u, got2 & 0x271fu, 0x271fu);
}

static void test_immediate_alu_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    wr32(IMM_TEST_BASE + 0x00u, 0x12a03400u);
    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "ori.b #0x0f,0x01ffa601\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00095000u, rd32(IMM_TEST_BASE + 0x00u), 0x12af3400u);
    chk32(0x00095004u, got0 & 0x1fu, 0x18u);

    wr32(IMM_TEST_BASE + 0x04u, 0x1234f0f0u);
    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "andi.w #0x0ff0,0x01ffa606\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00095008u, rd32(IMM_TEST_BASE + 0x04u), 0x123400f0u);
    chk32(0x0009500cu, got0 & 0x1fu, 0x10u);

    __asm__ volatile(
        "move.l #0xffff0000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "eori.l #0x0ff00ff0,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00095010u, got0, 0xf00f0ff0u);
    chk32(0x00095014u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x1234567f,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "addi.b #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00095018u, got0, 0x12345680u);
    chk32(0x0009501cu, got1 & 0x1fu, 0x0au);

    wr32(IMM_TEST_BASE + 0x10u, 0xaaaaffffu);
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "addi.w #1,0x01ffa612\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00095020u, rd32(IMM_TEST_BASE + 0x10u), 0xaaaa0000u);
    chk32(0x00095024u, got0 & 0x1fu, 0x15u);

    __asm__ volatile(
        "move.l #0x80000000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "subi.l #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00095028u, got0, 0x7fffffffu);
    chk32(0x0009502cu, got1 & 0x1fu, 0x02u);

    wr32(IMM_TEST_BASE + 0x18u, 0x00000001u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "subi.b #1,0x01ffa61b\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00095030u, rd32(IMM_TEST_BASE + 0x18u), 0x00000000u);
    chk32(0x00095034u, got0 & 0x1fu, 0x04u);

    __asm__ volatile(
        "move.l #0xaaaa1234,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpi.w #0x1234,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00095038u, got0, 0xaaaa1234u);
    chk32(0x0009503cu, got1 & 0x1fu, 0x14u);

    wr32(IMM_TEST_BASE + 0x20u, 0x00000010u);
    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "cmpi.l #0x00000020,0x01ffa620\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc", "memory");
    chk32(0x00095040u, rd32(IMM_TEST_BASE + 0x20u), 0x00000010u);
    chk32(0x00095044u, got0 & 0x1fu, 0x19u);
}

static void test_addx_subx_cmpm_memory_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;

    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x11u) = 0xffu;
    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x21u) = 0x00u;
    __asm__ volatile(
        "lea 0x01ffa712,%%a0\n\t"
        "lea 0x01ffa722,%%a1\n\t"
        "move.w #0x14,%%ccr\n\t"
        "addx.b -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096000u, got0, XMEM_TEST_BASE + 0x11u);
    chk32(0x00096004u, got1, XMEM_TEST_BASE + 0x21u);
    chk8(0x00096008u, rd8(XMEM_TEST_BASE + 0x21u), 0x00u);
    chk32(0x0009600cu, got2 & 0x1fu, 0x15u);

    *(volatile uint16_t *)(XMEM_TEST_BASE + 0x32u) = 0x7fffu;
    *(volatile uint16_t *)(XMEM_TEST_BASE + 0x42u) = 0x0000u;
    __asm__ volatile(
        "lea 0x01ffa734,%%a0\n\t"
        "lea 0x01ffa744,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "addx.w -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096010u, got0, XMEM_TEST_BASE + 0x32u);
    chk32(0x00096014u, got1, XMEM_TEST_BASE + 0x42u);
    chk16(0x00096018u, rd16(XMEM_TEST_BASE + 0x42u), 0x7fffu);
    chk32(0x0009601cu, got2 & 0x1fu, 0x00u);

    wr32(XMEM_TEST_BASE + 0x50u, 0x00000001u);
    wr32(XMEM_TEST_BASE + 0x60u, 0x00000002u);
    __asm__ volatile(
        "lea 0x01ffa754,%%a0\n\t"
        "lea 0x01ffa764,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "addx.l -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096020u, got0, XMEM_TEST_BASE + 0x50u);
    chk32(0x00096024u, got1, XMEM_TEST_BASE + 0x60u);
    chk32(0x00096028u, rd32(XMEM_TEST_BASE + 0x60u), 0x00000004u);
    chk32(0x0009602cu, got2 & 0x1fu, 0x00u);

    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x71u) = 0x00u;
    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x81u) = 0x00u;
    __asm__ volatile(
        "lea 0x01ffa772,%%a0\n\t"
        "lea 0x01ffa782,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "subx.b -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096030u, got0, XMEM_TEST_BASE + 0x71u);
    chk32(0x00096034u, got1, XMEM_TEST_BASE + 0x81u);
    chk8(0x00096038u, rd8(XMEM_TEST_BASE + 0x81u), 0xffu);
    chk32(0x0009603cu, got2 & 0x1fu, 0x19u);

    *(volatile uint16_t *)(XMEM_TEST_BASE + 0x92u) = 0x0001u;
    *(volatile uint16_t *)(XMEM_TEST_BASE + 0xa2u) = 0x8000u;
    __asm__ volatile(
        "lea 0x01ffa794,%%a0\n\t"
        "lea 0x01ffa7a4,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "subx.w -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096040u, got0, XMEM_TEST_BASE + 0x92u);
    chk32(0x00096044u, got1, XMEM_TEST_BASE + 0xa2u);
    chk16(0x00096048u, rd16(XMEM_TEST_BASE + 0xa2u), 0x7fffu);
    chk32(0x0009604cu, got2 & 0x1fu, 0x02u);

    wr32(XMEM_TEST_BASE + 0xb0u, 0x00000000u);
    wr32(XMEM_TEST_BASE + 0xc0u, 0x00000001u);
    __asm__ volatile(
        "lea 0x01ffa7b4,%%a0\n\t"
        "lea 0x01ffa7c4,%%a1\n\t"
        "move.w #0x14,%%ccr\n\t"
        "subx.l -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096050u, got0, XMEM_TEST_BASE + 0xb0u);
    chk32(0x00096054u, got1, XMEM_TEST_BASE + 0xc0u);
    chk32(0x00096058u, rd32(XMEM_TEST_BASE + 0xc0u), 0x00000000u);
    chk32(0x0009605cu, got2 & 0x1fu, 0x04u);

    *(volatile uint8_t *)(XMEM_TEST_BASE + 0xd0u) = 0x10u;
    *(volatile uint8_t *)(XMEM_TEST_BASE + 0xe0u) = 0x20u;
    __asm__ volatile(
        "lea 0x01ffa7d0,%%a0\n\t"
        "lea 0x01ffa7e0,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpm.b (%%a0)+,(%%a1)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096060u, got0, XMEM_TEST_BASE + 0xd1u);
    chk32(0x00096064u, got1, XMEM_TEST_BASE + 0xe1u);
    chk32(0x00096068u, got2 & 0x1fu, 0x10u);

    *(volatile uint16_t *)(XMEM_TEST_BASE + 0xf0u) = 0x8000u;
    *(volatile uint16_t *)(XMEM_TEST_BASE + 0x100u) = 0x7fffu;
    __asm__ volatile(
        "lea 0x01ffa7f0,%%a0\n\t"
        "lea 0x01ffa800,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpm.w (%%a0)+,(%%a1)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096070u, got0, XMEM_TEST_BASE + 0xf2u);
    chk32(0x00096074u, got1, XMEM_TEST_BASE + 0x102u);
    chk32(0x00096078u, got2 & 0x1fu, 0x1bu);

    wr32(XMEM_TEST_BASE + 0x110u, 0x12345678u);
    wr32(XMEM_TEST_BASE + 0x120u, 0x12345678u);
    __asm__ volatile(
        "lea 0x01ffa810,%%a0\n\t"
        "lea 0x01ffa820,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpm.l (%%a0)+,(%%a1)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096080u, got0, XMEM_TEST_BASE + 0x114u);
    chk32(0x00096084u, got1, XMEM_TEST_BASE + 0x124u);
    chk32(0x00096088u, got2 & 0x1fu, 0x14u);
}

static void test_system_control_directed(void)
{
    uint32_t got;
    uint32_t got_sp0;
    uint32_t got_sp1;
    uint32_t pc_after_trap;

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "cc");
    chk32(0x000a0000u, got & 0xff00u, 0x2700u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0x15,%%ccr\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "cc");
    chk32(0x000a0001u, got & 0x1fu, 0x15u);

    wr32(RETURN_TEST_BASE + 0xc4u, 0x55555555u);
    wr32(RETURN_TEST_BASE + 0xc8u, 0x00140000u);
    __asm__ volatile(
        "lea 0x01ff99c4,%%a0\n\t"
        "move.w #0x1b,%%ccr\n\t"
        "move.w %%ccr,(%%a0)\n\t"
        "lea 0x01ff99c8,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.w (%%a0),%%ccr\n\t"
        "moveq #0,%%d0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "a0", "d0", "cc", "memory");
    chk32(0x000a0004u, rd32(RETURN_TEST_BASE + 0xc4u), 0x001b5555u);
    chk32(0x000a0008u, got & 0xff00u, 0x2700u);
    chk32(0x000a000cu, got & 0x1fu, 0x14u);

    __asm__ volatile(
        "movec %%vbr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0");
    chk32(0x000a0010u, got, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "movec %%vbr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0");
    chk32(0x000a0011u, got, 0u);

    wr32(RETURN_TEST_BASE + 0xccu, 0xaaaaaaaau);
    wr32(RETURN_TEST_BASE + 0xd0u, 0x271b0000u);
    __asm__ volatile(
        "move.l #0xaaaa5555,%%d0\n\t"
        "move.w #0x0015,%%ccr\n\t"
        "move.w %%ccr,%%d0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l #0x001a,%%d1\n\t"
        "move.w %%d1,%%ccr\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,%1\n\t"
        "lea 0x01ff99cc,%%a0\n\t"
        "move.w #0x0015,%%ccr\n\t"
        "move.w %%sr,(%%a0)\n\t"
        "lea 0x01ff99d0,%%a0\n\t"
        "move.w (%%a0),%%sr\n\t"
        "move.w %%sr,%%d3\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%d3,%2"
        : "=&d"(got), "=&d"(got_sp0), "=&d"(got_sp1)
        :
        : "a0", "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000a0070u, got, 0xaaaa0015u);
    chk32(0x000a0074u, got_sp0 & 0x271fu, 0x271au);
    chk32(0x000a0078u, rd32(RETURN_TEST_BASE + 0xccu), 0x2715aaaau);
    chk32(0x000a007cu, got_sp1 & 0x271fu, 0x271bu);

    __asm__ volatile(
        "move.w #0x2700,%%sr\n\t"
        "ori.w #0x0015,%%sr\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.w #0x271f,%%sr\n\t"
        "andi.w #0x270a,%%sr\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.w #0x2705,%%sr\n\t"
        "eori.w #0x000f,%%sr\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got), "=&d"(got_sp0), "=&d"(got_sp1)
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000a0080u, got & 0x271fu, 0x2715u);
    chk32(0x000a0084u, got_sp0 & 0x271fu, 0x270au);
    chk32(0x000a0088u, got_sp1 & 0x271fu, 0x270au);

    __asm__ volatile(
        "moveq #5,%%d0\n\t"
        "movec %%d0,%%sfc\n\t"
        "moveq #6,%%d1\n\t"
        "movec %%d1,%%dfc\n\t"
        "movec %%sfc,%%d2\n\t"
        "movec %%dfc,%%d3\n\t"
        "move.l %%d2,%0\n\t"
        "move.l %%d3,%1\n\t"
        "moveq #1,%%d0\n\t"
        "movec %%d0,%%sfc\n\t"
        "movec %%d0,%%dfc"
        : "=&d"(got), "=&d"(got_sp0)
        :
        : "d0", "d1", "d2", "d3");
    chk32(0x000a0014u, got, 5u);
    chk32(0x000a0016u, got_sp0, 6u);

    for (uint32_t off = 0xb0u; off <= 0xbcu; off += 4u) {
        wr32(RETURN_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #2,%%d6\n\t"
        "movec %%d6,%%sfc\n\t"
        "movec %%vbr,%%d7\n\t"
        "movec %%sfc,%%d0\n\t"
        "move.l %%d0,0x01ff99b0\n\t"
        "moveq #3,%%d6\n\t"
        "movec %%d6,%%sfc\n\t"
        "movec %%dfc,%%d7\n\t"
        "movec %%sfc,%%d0\n\t"
        "move.l %%d0,0x01ff99b4\n\t"
        "moveq #4,%%d6\n\t"
        "movec %%d6,%%dfc\n\t"
        "movec %%sfc,%%d7\n\t"
        "movec %%dfc,%%d0\n\t"
        "move.l %%d0,0x01ff99b8\n\t"
        "moveq #5,%%d6\n\t"
        "movec %%d6,%%dfc\n\t"
        "movec %%vbr,%%d7\n\t"
        "movec %%dfc,%%d0\n\t"
        "move.l %%d0,0x01ff99bc\n\t"
        "moveq #1,%%d6\n\t"
        "movec %%d6,%%sfc\n\t"
        "movec %%d6,%%dfc"
        :
        :
        : "d0", "d6", "d7", "cc", "memory");
    chk32(0x000a0060u, rd32(RETURN_TEST_BASE + 0xb0u), 2u);
    chk32(0x000a0064u, rd32(RETURN_TEST_BASE + 0xb4u), 3u);
    chk32(0x000a0068u, rd32(RETURN_TEST_BASE + 0xb8u), 4u);
    chk32(0x000a006cu, rd32(RETURN_TEST_BASE + 0xbcu), 5u);

    __asm__ volatile(
        "move.l %%sp,%0\n\t"
        "lea 0x01ff9e00,%%a0\n\t"
        "move.l %%a0,%%usp\n\t"
        "suba.l %%a1,%%a1\n\t"
        "move.l %%usp,%%a1\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%sp,%2"
        : "=&d"(got_sp0), "=&d"(got), "=&d"(got_sp1)
        :
        : "a0", "a1", "memory");
    chk32(0x000a0018u, got, 0x01ff9e00u);
    chk32(0x000a001cu, got_sp1, got_sp0);

    wr32(RETURN_TEST_BASE + 0x5cu, 0u);
    wr32(RETURN_TEST_BASE + 0x7cu, 0u);
    for (uint32_t off = 0x90u; off <= 0xa4u; off += 4u) {
        wr32(RETURN_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "movec %%msp,%%d4\n\t"
        "move.l #0x01ff9960,%%d0\n\t"
        "move.l #0x01ff9980,%%d1\n\t"
        "movec %%d0,%%isp\n\t"
        "movec %%d1,%%msp\n\t"
        "move.w #0x3700,%%sr\n\t"
        "move.l %%sp,0x01ff9990\n\t"
        "move.l #0x5a5aa55a,-(%%sp)\n\t"
        "move.l %%sp,0x01ff9994\n\t"
        "movec %%msp,%%d2\n\t"
        "move.l %%d2,0x01ff9998\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%sp,0x01ff999c\n\t"
        "move.l #0xa55a5aa5,-(%%sp)\n\t"
        "move.l %%sp,0x01ff99a0\n\t"
        "movec %%isp,%%d3\n\t"
        "move.l %%d3,0x01ff99a4\n\t"
        "move.l %%a2,%%sp\n\t"
        "movec %%d4,%%msp"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0040u, rd32(RETURN_TEST_BASE + 0x90u), RETURN_TEST_BASE + 0x80u);
    chk32(0x000a0044u, rd32(RETURN_TEST_BASE + 0x94u), RETURN_TEST_BASE + 0x7cu);
    chk32(0x000a0048u, rd32(RETURN_TEST_BASE + 0x98u), RETURN_TEST_BASE + 0x7cu);
    chk32(0x000a004cu, rd32(RETURN_TEST_BASE + 0x9cu), RETURN_TEST_BASE + 0x60u);
    chk32(0x000a0050u, rd32(RETURN_TEST_BASE + 0xa0u), RETURN_TEST_BASE + 0x5cu);
    chk32(0x000a0054u, rd32(RETURN_TEST_BASE + 0xa4u), RETURN_TEST_BASE + 0x5cu);
    chk32(0x000a0058u, rd32(RETURN_TEST_BASE + 0x7cu), 0x5a5aa55au);
    chk32(0x000a005cu, rd32(RETURN_TEST_BASE + 0x5cu), 0xa55a5aa5u);

    wr32(SCRATCH_BASE + 0xf0, 0u);
    wr32(SCRATCH_BASE + 0xf4, 0u);
    wr32(SCRATCH_BASE + 0xf8, 0u);
    wr32(SCRATCH_BASE + 0xfc, 0u);
    wr32(SCRATCH_BASE + 0x100, 0u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,%0\n\t"
        "move.w #0x15,%%ccr\n\t"
        "trap #0\n"
        "1:"
        : "=d"(pc_after_trap)
        :
        : "a0", "cc", "memory");
    chk32(0x000a0020u, rd32(SCRATCH_BASE + 0xf0), 1u);
    chk32(0x000a0024u, rd32(SCRATCH_BASE + 0xf4), 0x2715u);
    chk32(0x000a0028u, rd32(SCRATCH_BASE + 0xf8), pc_after_trap);
    chk32(0x000a002cu, rd32(SCRATCH_BASE + 0xfc), 0x0080u);
    chk32(0x000a0030u, rd32(SCRATCH_BASE + 0x100) & 1u, 0u);
    __asm__ volatile("trap #0" : : : "a0", "memory");
    chk32(0x000a0034u, rd32(SCRATCH_BASE + 0xf0), 2u);
}

static void test_moves_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;

    wr32(MOVES_TEST_BASE + 0x00u, 0u);
    wr32(MOVES_TEST_BASE + 0x04u, 0u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff9800,%%a0\n\t"
        "move.l #0xa5a55a5a,%%d0\n\t"
        "moves.l %%d0,(%%a0)\n\t"
        "moveq #0,%%d1\n\t"
        "moves.l (%%a0),%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "movea.l #0x01020304,%%a1\n\t"
        "moves.l %%a1,4(%%a0)\n\t"
        "suba.l %%a2,%%a2\n\t"
        "moves.l 4(%%a0),%%a2\n\t"
        "move.l %%a2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "a2", "d0", "d1", "d2", "memory");
    chk32(0x000a0100u, rd32(MOVES_TEST_BASE + 0x00u), 0xa5a55a5au);
    chk32(0x000a0104u, got0, 0xa5a55a5au);
    chk32(0x000a0108u, rd32(MOVES_TEST_BASE + 0x04u), 0x01020304u);
    chk32(0x000a010cu, got1, 0x01020304u);

    wr32(MOVES_TEST_BASE + 0x10u, 0u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff9811,%%a0\n\t"
        "moveq #0,%%d0\n\t"
        "move.b #0xa5,%%d0\n\t"
        "moves.b %%d0,(%%a0)+\n\t"
        "move.l %%a0,%0\n\t"
        "subq.l #1,%%a0\n\t"
        "moveq #0,%%d1\n\t"
        "moves.b (%%a0)+,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d2", "memory");
    chk32(0x000a0120u, rd32(MOVES_TEST_BASE + 0x10u), 0x00a50000u);
    chk32(0x000a0124u, got0, MOVES_TEST_BASE + 0x12u);
    chk32(0x000a0128u, got1, 0x000000a5u);
    chk32(0x000a012cu, got2, MOVES_TEST_BASE + 0x12u);

    wr32(MOVES_TEST_BASE + 0x20u, 0u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff9822,%%a0\n\t"
        "move.w #0xb6c7,%%d0\n\t"
        "moves.w %%d0,-(%%a0)\n\t"
        "move.l %%a0,%0\n\t"
        "lea 0x01ff9820,%%a1\n\t"
        "moveq #0,%%d1\n\t"
        "moves.w (%%a1)+,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "d1", "d2", "memory");
    chk32(0x000a0130u, rd32(MOVES_TEST_BASE + 0x20u), 0xb6c70000u);
    chk32(0x000a0134u, got0, MOVES_TEST_BASE + 0x20u);
    chk32(0x000a0138u, got1, 0x0000b6c7u);
    chk32(0x000a013cu, got2, MOVES_TEST_BASE + 0x22u);
}

static void test_cmp2_chk2_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;

    wr32(BOUNDS_TEST_BASE + 0x00u, 0x00000010u);
    wr32(BOUNDS_TEST_BASE + 0x04u, 0x00000020u);
    __asm__ volatile(
        "lea 0x01ff9820,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x10,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x15,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x30,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0200u, got0 & 0x1fu, 0x04u);
    chk32(0x000a0204u, got1 & 0x1fu, 0x00u);
    chk32(0x000a0208u, got2 & 0x1fu, 0x01u);

    wr32(BOUNDS_TEST_BASE + 0x10u, 0x00000020u);
    wr32(BOUNDS_TEST_BASE + 0x14u, 0x00000010u);
    __asm__ volatile(
        "lea 0x01ff9830,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x05,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x15,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x25,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%2\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x20,%%d0\n\t"
        "cmp2.l (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0220u, got0 & 0x1fu, 0x00u);
    chk32(0x000a0224u, got1 & 0x1fu, 0x01u);
    chk32(0x000a0228u, got2 & 0x1fu, 0x00u);
    chk32(0x000a022cu, got3 & 0x1fu, 0x04u);

    wr32(BOUNDS_TEST_BASE + 0x18u, 0x01ff9900u);
    wr32(BOUNDS_TEST_BASE + 0x1cu, 0x01ff9910u);
    __asm__ volatile(
        "lea 0x01ff9838,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "movea.l #0x01ff9900,%%a1\n\t"
        "cmp2.l (%%a0),%%a1\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.w #0,%%ccr\n\t"
        "movea.l #0x01ff9908,%%a1\n\t"
        "cmp2.l (%%a0),%%a1\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.w #0,%%ccr\n\t"
        "movea.l #0x01ff9920,%%a1\n\t"
        "cmp2.l (%%a0),%%a1\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d1", "cc", "memory");
    chk32(0x000a0230u, got0 & 0x1fu, 0x04u);
    chk32(0x000a0234u, got1 & 0x1fu, 0x00u);
    chk32(0x000a0238u, got2 & 0x1fu, 0x01u);

    *(volatile uint16_t *)(BOUNDS_TEST_BASE + 0x40u) = 0x0010u;
    *(volatile uint16_t *)(BOUNDS_TEST_BASE + 0x42u) = 0x0020u;
    __asm__ volatile(
        "lea 0x01ff9860,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x10,%%d0\n\t"
        "cmp2.w (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x15,%%d0\n\t"
        "cmp2.w (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x30,%%d0\n\t"
        "cmp2.w (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0250u, got0 & 0x1fu, 0x04u);
    chk32(0x000a0254u, got1 & 0x1fu, 0x00u);
    chk32(0x000a0258u, got2 & 0x1fu, 0x01u);

    *(volatile uint8_t *)(BOUNDS_TEST_BASE + 0x50u) = 0x10u;
    *(volatile uint8_t *)(BOUNDS_TEST_BASE + 0x51u) = 0x20u;
    __asm__ volatile(
        "lea 0x01ff9870,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x10,%%d0\n\t"
        "cmp2.b (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x15,%%d0\n\t"
        "cmp2.b (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l #0x30,%%d0\n\t"
        "cmp2.b (%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0260u, got0 & 0x1fu, 0x04u);
    chk32(0x000a0264u, got1 & 0x1fu, 0x00u);
    chk32(0x000a0268u, got2 & 0x1fu, 0x01u);

    got0 = 0u;
    __asm__ volatile(
        "lea 0x01ff9820,%%a0\n\t"
        "move.l #0x15,%%d0\n\t"
        "chk2.l (%%a0),%%d0\n\t"
        "moveq #0x5a,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0210u, got0, 0x5au);

    got0 = 0u;
    __asm__ volatile(
        "lea 0x01ff9838,%%a0\n\t"
        "movea.l #0x01ff9908,%%a1\n\t"
        "chk2.l (%%a0),%%a1\n\t"
        "moveq #0x5b,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "a1", "d1", "cc", "memory");
    chk32(0x000a0240u, got0, 0x5bu);

    got0 = 0u;
    __asm__ volatile(
        "lea 0x01ff9860,%%a0\n\t"
        "move.l #0x15,%%d0\n\t"
        "chk2.w (%%a0),%%d0\n\t"
        "moveq #0x5c,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0270u, got0, 0x5cu);

    got0 = 0u;
    __asm__ volatile(
        "lea 0x01ff9870,%%a0\n\t"
        "move.l #0x15,%%d0\n\t"
        "chk2.b (%%a0),%%d0\n\t"
        "moveq #0x5d,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000a0274u, got0, 0x5du);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x18u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "lea 0x01ff9820,%%a0\n\t"
        "move.l #0x30,%%d0\n\t"
        "chk2.l (%%a0),%%d0\n"
        "1:\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x00120000u, 0x0018u, 0x2000u, 0x2700u, 0x2700u);

    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "lea 0x01ff9860,%%a0\n\t"
        "move.l #0x30,%%d0\n\t"
        "chk2.w (%%a0),%%d0\n"
        "1:\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x00120020u, 0x0018u, 0x2000u, 0x2700u, 0x2700u);
}

static void test_chk_directed(void)
{
    uint32_t got;

    *(volatile uint16_t *)(CHK_TEST_BASE + 0x00u) = 10u;
    got = 0u;
    __asm__ volatile(
        "lea 0x01ffab00,%%a0\n\t"
        "moveq #0,%%d0\n\t"
        "chk.w (%%a0),%%d0\n\t"
        "moveq #0x5c,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x00120100u, got, 0x5cu);

    got = 0u;
    __asm__ volatile(
        "lea 0x01ffab00,%%a0\n\t"
        "moveq #10,%%d0\n\t"
        "chk.w (%%a0),%%d0\n\t"
        "moveq #0x5d,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x00120104u, got, 0x5du);

    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #-1,%%d0\n\t"
        "chk.w 0x01ffab00,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00120120u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #11,%%d0\n\t"
        "chk.w 0x01ffab00,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00120140u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);

    wr32(CHK_TEST_BASE + 0x10u, 0x00010000u);
    got = 0u;
    __asm__ volatile(
        "lea 0x01ffab10,%%a0\n\t"
        "move.l #0x00010000,%%d0\n\t"
        "chk.l (%%a0),%%d0\n\t"
        "moveq #0x5e,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x00120160u, got, 0x5eu);

    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #-1,%%d0\n\t"
        "chk.l 0x01ffab10,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00120180u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x00010001,%%d0\n\t"
        "chk.l 0x01ffab10,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x001201a0u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);
}

static void test_cas2_directed(void)
{
    uint32_t got;

    wr32(CAS2_TEST_BASE + 0x00u, 0x11111111u);
    wr32(CAS2_TEST_BASE + 0x04u, 0x22222222u);
    __asm__ volatile(
        "lea 0x01ff9840,%%a0\n\t"
        "lea 0x01ff9844,%%a1\n\t"
        "move.l #0x11111111,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x22222222,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0300u, rd32(CAS2_TEST_BASE + 0x00u), 0xaaaaaaaau);
    chk32(0x000a0304u, rd32(CAS2_TEST_BASE + 0x04u), 0xbbbbbbbbu);
    chk32(0x000a0308u, got & 0x1fu, 0x04u);

    wr32(CAS2_TEST_BASE + 0x08u, 0x33333333u);
    wr32(CAS2_TEST_BASE + 0x0cu, 0x44444444u);
    __asm__ volatile(
        "lea 0x01ff9848,%%a0\n\t"
        "lea 0x01ff984c,%%a1\n\t"
        "move.l #0x33333333,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x99999999,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0310u, rd32(CAS2_TEST_BASE + 0x08u), 0x33333333u);
    chk32(0x000a0314u, rd32(CAS2_TEST_BASE + 0x0cu), 0x44444444u);
    chk32(0x000a0318u, got & 0x04u, 0x00u);

    wr32(CAS2_TEST_BASE + 0x10u, 0x55555555u);
    wr32(CAS2_TEST_BASE + 0x14u, 0x66666666u);
    wr32(CAS2_TEST_BASE + 0x18u, 0u);
    wr32(CAS2_TEST_BASE + 0x1cu, 0u);
    __asm__ volatile(
        "lea 0x01ff9850,%%a0\n\t"
        "lea 0x01ff9854,%%a1\n\t"
        "move.l #0x55555555,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x99999999,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d0,0x01ff9858\n\t"
        "move.l %%d2,0x01ff985c\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0320u, rd32(CAS2_TEST_BASE + 0x10u), 0x55555555u);
    chk32(0x000a0324u, rd32(CAS2_TEST_BASE + 0x14u), 0x66666666u);
    chk32(0x000a0328u, rd32(CAS2_TEST_BASE + 0x18u), 0x55555555u);
    chk32(0x000a032cu, rd32(CAS2_TEST_BASE + 0x1cu), 0x66666666u);
    chk32(0x000a0330u, got & 0x04u, 0x00u);

    wr32(CAS2_TEST_BASE + 0x20u, 0x11111111u);
    wr32(CAS2_TEST_BASE + 0x24u, 0x33333333u);
    wr32(CAS2_TEST_BASE + 0x28u, 0u);
    wr32(CAS2_TEST_BASE + 0x2cu, 0u);
    __asm__ volatile(
        "lea 0x01ff9860,%%a0\n\t"
        "lea 0x01ff9864,%%a1\n\t"
        "move.l #0x22222222,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x33333333,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d0,0x01ff9868\n\t"
        "move.l %%d2,0x01ff986c\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0340u, rd32(CAS2_TEST_BASE + 0x20u), 0x11111111u);
    chk32(0x000a0344u, rd32(CAS2_TEST_BASE + 0x24u), 0x33333333u);
    chk32(0x000a0348u, rd32(CAS2_TEST_BASE + 0x28u), 0x11111111u);
    chk32(0x000a034cu, rd32(CAS2_TEST_BASE + 0x2cu), 0x33333333u);
    chk32(0x000a0350u, got & 0x04u, 0x00u);

    wr32(CAS2_TEST_BASE + 0x40u, 0x01010101u);
    wr32(CAS2_TEST_BASE + 0x44u, 0x02020202u);
    __asm__ volatile(
        "move.l #0x01ff9880,%%d4\n\t"
        "move.l #0x01ff9884,%%d5\n\t"
        "move.l #0x01010101,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x02020202,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%d4):(%%d5)\n\t"
        "move.w %%sr,%%d6\n\t"
        "move.l %%d6,%0"
        : "=&d"(got)
        :
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "cc", "memory");
    chk32(0x000a0360u, rd32(CAS2_TEST_BASE + 0x40u), 0xaaaaaaaau);
    chk32(0x000a0364u, rd32(CAS2_TEST_BASE + 0x44u), 0xbbbbbbbbu);
    chk32(0x000a0368u, got & 0x1fu, 0x04u);

    wr32(CAS2_TEST_BASE + 0x48u, 0x03030303u);
    wr32(CAS2_TEST_BASE + 0x4cu, 0x04040404u);
    wr32(CAS2_TEST_BASE + 0x50u, 0u);
    wr32(CAS2_TEST_BASE + 0x54u, 0u);
    __asm__ volatile(
        "move.l #0x01ff9888,%%d4\n\t"
        "move.l #0x01ff988c,%%d5\n\t"
        "move.l #0x03030303,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x99999999,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%d4):(%%d5)\n\t"
        "move.w %%sr,%%d6\n\t"
        "move.l %%d0,0x01ff9890\n\t"
        "move.l %%d2,0x01ff9894\n\t"
        "move.l %%d6,%0"
        : "=&d"(got)
        :
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "cc", "memory");
    chk32(0x000a0370u, rd32(CAS2_TEST_BASE + 0x48u), 0x03030303u);
    chk32(0x000a0374u, rd32(CAS2_TEST_BASE + 0x4cu), 0x04040404u);
    chk32(0x000a0378u, rd32(CAS2_TEST_BASE + 0x50u), 0x03030303u);
    chk32(0x000a037cu, rd32(CAS2_TEST_BASE + 0x54u), 0x04040404u);
    chk32(0x000a0380u, got & 0x04u, 0x00u);

    wr32(CAS2_TEST_BASE + 0x60u, 0x11112222u);
    wr32(CAS2_TEST_BASE + 0x64u, 0x33334444u);
    __asm__ volatile(
        "lea 0x01ff98a0,%%a0\n\t"
        "lea 0x01ff98a4,%%a1\n\t"
        "move.l #0xffff1111,%%d0\n\t"
        "move.l #0xeeeeaaaa,%%d1\n\t"
        "move.l #0xdddd3333,%%d2\n\t"
        "move.l #0xccccbbbb,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "cas2.w %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a0390u, rd32(CAS2_TEST_BASE + 0x60u), 0xaaaa2222u);
    chk32(0x000a0394u, rd32(CAS2_TEST_BASE + 0x64u), 0xbbbb4444u);
    chk32(0x000a0398u, got & 0x1fu, 0x04u);

    wr32(CAS2_TEST_BASE + 0x70u, 0x12345678u);
    wr32(CAS2_TEST_BASE + 0x74u, 0x9abcdef0u);
    wr32(CAS2_TEST_BASE + 0x78u, 0u);
    wr32(CAS2_TEST_BASE + 0x7cu, 0u);
    __asm__ volatile(
        "lea 0x01ff98b0,%%a0\n\t"
        "lea 0x01ff98b4,%%a1\n\t"
        "move.l #0xaaaa1111,%%d0\n\t"
        "move.l #0x1111aaaa,%%d1\n\t"
        "move.l #0xbbbb2222,%%d2\n\t"
        "move.l #0x2222bbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.w %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d0,0x01ff98b8\n\t"
        "move.l %%d2,0x01ff98bc\n\t"
        "move.l %%d4,%0"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000a03a0u, rd32(CAS2_TEST_BASE + 0x70u), 0x12345678u);
    chk32(0x000a03a4u, rd32(CAS2_TEST_BASE + 0x74u), 0x9abcdef0u);
    chk16(0x000a03a8u, rd32(CAS2_TEST_BASE + 0x78u), 0x1234u);
    chk16(0x000a03acu, rd32(CAS2_TEST_BASE + 0x7cu), 0x9abcu);
    chk32(0x000a03b0u, got & 0x04u, 0x00u);
}

static void test_return_control_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9900\n\t"
        "move.l #0xdeadbeef,0x01ff9904\n\t"
        "lea 0x01ff9900,%%sp\n\t"
        "rtd #4\n"
        "1:\n\t"
        "move.l %%sp,%0\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0)
        :
        : "a0", "a2", "memory");
    chk32(0x000a0400u, got0, RETURN_TEST_BASE + 0x08u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9910\n\t"
        "move.l #0xdeadbeef,0x01ff9914\n\t"
        "lea 0x01ff9910,%%sp\n\t"
        "rtd #-2\n"
        "1:\n\t"
        "move.l %%sp,%0\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0)
        :
        : "a0", "a2", "memory");
    chk32(0x000a0408u, got0, RETURN_TEST_BASE + 0x12u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "move.l #0xaaaaaaaa,%%d0\n\t"
        "lea 0x01ff9920,%%a0\n\t"
        "move.w #0x0015,(%%a0)\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,2(%%a0)\n\t"
        "move.w #0,%%ccr\n\t"
        "move.l %%a0,%%sp\n\t"
        "rtr\n"
        "1:\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%sp,%1\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "a2", "d0", "cc", "memory");
    chk32(0x000a0410u, got0 & 0x1fu, 0x15u);
    chk32(0x000a0411u, got0 & 0xff00u, 0x2700u);
    chk32(0x000a0414u, got1, RETURN_TEST_BASE + 0x26u);

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff9930,%%a0\n\t"
        "move.w #0x2015,(%%a0)\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,2(%%a0)\n\t"
        "move.w #0,6(%%a0)\n\t"
        "move.l %%a0,%%sp\n\t"
        "rte\n"
        "1:\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%sp,%1\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "a2", "d0", "cc", "memory");
    chk32(0x000a0418u, got0 & 0x271fu, 0x2015u);
    chk32(0x000a041cu, got1, RETURN_TEST_BASE + 0x38u);
}

static void test_bcd_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;

    __asm__ volatile(
        "moveq #0x15,%%d0\n\t"
        "moveq #0x27,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "abcd %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk8(0x000a0500u, got0, 0x42u);
    chk32(0x000a0504u, got1 & 0x15u, 0x00u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.b #0x99,%%d0\n\t"
        "moveq #0x00,%%d1\n\t"
        "move.w #0x0014,%%ccr\n\t"
        "abcd %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk8(0x000a0510u, got0, 0x00u);
    chk32(0x000a0514u, got1 & 0x15u, 0x15u);

    __asm__ volatile(
        "moveq #0x15,%%d0\n\t"
        "moveq #0x42,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "sbcd %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk8(0x000a0520u, got0, 0x27u);
    chk32(0x000a0524u, got1 & 0x15u, 0x00u);

    __asm__ volatile(
        "moveq #0x01,%%d0\n\t"
        "moveq #0x00,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "sbcd %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk8(0x000a0530u, got0, 0x99u);
    chk32(0x000a0534u, got1 & 0x15u, 0x11u);

    __asm__ volatile(
        "moveq #0x45,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "nbcd %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk8(0x000a0540u, got0, 0x55u);
    chk32(0x000a0544u, got1 & 0x15u, 0x11u);

    *(volatile uint8_t *)(BCD_TEST_BASE + 0x21u) = 0x15u;
    *(volatile uint8_t *)(BCD_TEST_BASE + 0x31u) = 0x27u;
    __asm__ volatile(
        "lea 0x01ff9a22,%%a0\n\t"
        "lea 0x01ff9a32,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "abcd -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d1", "cc", "memory");
    chk32(0x000a0550u, got0, BCD_TEST_BASE + 0x21u);
    chk32(0x000a0554u, got1, BCD_TEST_BASE + 0x31u);
    chk8(0x000a0558u, rd8(BCD_TEST_BASE + 0x31u), 0x42u);
    chk32(0x000a055cu, got2 & 0x15u, 0x00u);

    *(volatile uint8_t *)(BCD_TEST_BASE + 0x41u) = 0x15u;
    *(volatile uint8_t *)(BCD_TEST_BASE + 0x51u) = 0x42u;
    __asm__ volatile(
        "lea 0x01ff9a42,%%a0\n\t"
        "lea 0x01ff9a52,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "sbcd -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d1", "cc", "memory");
    chk32(0x000a0560u, got0, BCD_TEST_BASE + 0x41u);
    chk32(0x000a0564u, got1, BCD_TEST_BASE + 0x51u);
    chk8(0x000a0568u, rd8(BCD_TEST_BASE + 0x51u), 0x27u);
    chk32(0x000a056cu, got2 & 0x15u, 0x00u);
}

static void test_pack_unpk_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    *(volatile uint16_t *)(PACK_TEST_BASE + 0x20u) = 0x0205u;
    *(volatile uint8_t *)(PACK_TEST_BASE + 0x31u) = 0u;
    __asm__ volatile(
        "lea 0x01ff9b22,%%a0\n\t"
        "lea 0x01ff9b32,%%a1\n\t"
        "pack -(%%a0),-(%%a1),#0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0600u, got0, PACK_TEST_BASE + 0x20u);
    chk32(0x000a0604u, got1, PACK_TEST_BASE + 0x31u);
    chk8(0x000a0608u, rd8(PACK_TEST_BASE + 0x31u), 0x25u);

    *(volatile uint8_t *)(PACK_TEST_BASE + 0x41u) = 0x25u;
    *(volatile uint16_t *)(PACK_TEST_BASE + 0x50u) = 0u;
    __asm__ volatile(
        "lea 0x01ff9b42,%%a0\n\t"
        "lea 0x01ff9b52,%%a1\n\t"
        "unpk -(%%a0),-(%%a1),#0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0610u, got0, PACK_TEST_BASE + 0x41u);
    chk32(0x000a0614u, got1, PACK_TEST_BASE + 0x50u);
    chk16(0x000a0618u, rd16(PACK_TEST_BASE + 0x50u), 0x0205u);

    __asm__ volatile(
        "move.l #0x11220205,%%d0\n\t"
        "move.l #0xaabbccdd,%%d1\n\t"
        "pack %%d0,%%d1,#0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x000a0620u, got0, 0x11220205u);
    chk32(0x000a0624u, got1, 0xaabbcc25u);

    __asm__ volatile(
        "move.l #0x00000912,%%d0\n\t"
        "move.l #0x12345678,%%d1\n\t"
        "pack %%d0,%%d1,#0x0101\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x000a0628u, got0, 0x00000912u);
    chk32(0x000a062cu, got1, 0x123456a3u);

    __asm__ volatile(
        "move.l #0x99887725,%%d0\n\t"
        "move.l #0x12345678,%%d1\n\t"
        "unpk %%d0,%%d1,#0\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x000a0630u, got0, 0x99887725u);
    chk32(0x000a0634u, got1, 0x12340205u);

    __asm__ volatile(
        "move.l #0x99887725,%%d0\n\t"
        "move.l #0x89abcdef,%%d1\n\t"
        "unpk %%d0,%%d1,#0x0101\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x000a0638u, got0, 0x99887725u);
    chk32(0x000a063cu, got1, 0x89ab0306u);

    *(volatile uint16_t *)(PACK_TEST_BASE + 0x60u) = 0x0912u;
    *(volatile uint8_t *)(PACK_TEST_BASE + 0x71u) = 0u;
    __asm__ volatile(
        "lea 0x01ff9b62,%%a0\n\t"
        "lea 0x01ff9b72,%%a1\n\t"
        "pack -(%%a0),-(%%a1),#0x0101\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0640u, got0, PACK_TEST_BASE + 0x60u);
    chk32(0x000a0644u, got1, PACK_TEST_BASE + 0x71u);
    chk8(0x000a0648u, rd8(PACK_TEST_BASE + 0x71u), 0xa3u);

    *(volatile uint8_t *)(PACK_TEST_BASE + 0x81u) = 0x25u;
    *(volatile uint16_t *)(PACK_TEST_BASE + 0x90u) = 0u;
    __asm__ volatile(
        "lea 0x01ff9b82,%%a0\n\t"
        "lea 0x01ff9b92,%%a1\n\t"
        "unpk -(%%a0),-(%%a1),#0x0101\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0650u, got0, PACK_TEST_BASE + 0x81u);
    chk32(0x000a0654u, got1, PACK_TEST_BASE + 0x90u);
    chk16(0x000a0658u, rd16(PACK_TEST_BASE + 0x90u), 0x0306u);
}

static void test_exception_recovery_directed(void)
{
    arm_exception_recovery(0x000cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "lea 2f,%%a0\n\t"
        "addq.l #1,%%a0\n\t"
        "jmp (%%a0)\n"
        "2:\n\t"
        "nop\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_access_fault_frame(0x00100700u, 0x000cu, 0x2000u, 0x2000u, 1u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x10u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        ".word 0x4afc\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "memory");
    chk_exception_frame(0x00100000u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        ".word 0x4afc\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100020u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0014u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #1234,%%d0\n\t"
        "divu.w #0,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00100040u, 0x0014u, 0x2000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0014u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #1234,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "divu.l %%d1,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x001004c0u, 0x0014u, 0x2000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0014u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #-1234,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "divs.l %%d1,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x001004e0u, 0x0014u, 0x2000u, 0x2000u, 0x2000u);

    *(volatile uint16_t *)(SCRATCH_BASE + 0x1a0u) = 4u;
    arm_exception_recovery(0x0018u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #5,%%d0\n\t"
        "chk.w 0x01ff92a0,%%d0\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00100060u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x02,%%ccr\n\t"
        "trapv\n"
        "1:"
        :
        :
        : "a0", "cc", "memory");
    chk_exception_frame(0x00100080u, 0x001cu, 0x2000u, 0x201fu, 0x2002u);

    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "trapeq\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00100140u, 0x001cu, 0x2000u, 0x201fu, 0x2004u);

    wr32(SCRATCH_BASE + 0x168u, 0u);
    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cmp.l %%d1,%%d0\n\t"
        "trapeq\n"
        "1:\n\t"
        "moveq #0x6c,%%d2\n\t"
        "move.l %%d2,0x01ff9268"
        :
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x00100160u, rd32(EXC_REC_BASE + 0x00u), 0u);
    chk32(0x00100164u, rd32(SCRATCH_BASE + 0x168u), 0x6cu);

    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "trapeq.w #0x1234\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00100240u, 0x001cu, 0x2000u, 0x201fu, 0x2004u);

    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "trapeq.l #0x12345678\n"
        "1:"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00100260u, 0x001cu, 0x2000u, 0x201fu, 0x2004u);

    wr32(SCRATCH_BASE + 0x168u, 0u);
    arm_exception_recovery(0x001cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cmp.l %%d1,%%d0\n\t"
        "trapeq.l #0x12345678\n"
        "1:\n\t"
        "moveq #0x6d,%%d2\n\t"
        "move.l %%d2,0x01ff9268"
        :
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x00100280u, rd32(EXC_REC_BASE + 0x00u), 0u);
    chk32(0x00100284u, rd32(SCRATCH_BASE + 0x168u), 0x6du);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x9cu, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x009cu);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x15,%%ccr\n\t"
        "trap #7\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x001002a0u, 0x009cu, 0x0000u, 0x271fu, 0x2715u);

    arm_exception_recovery(0x0020u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0015,%%sr\n\t"
        "move.w #0x2700,%%sr\n"
        "1:"
        :
        :
        : "a0", "cc", "memory");
    chk_exception_frame(0x001000a0u, 0x0020u, 0x0000u, 0x201fu, 0x0015u);

#define EXPECT_PRIV_OP(ID, OPASM) \
    do { \
        arm_exception_recovery(0x0020u); \
        __asm__ volatile( \
            "lea 1f,%%a0\n\t" \
            "move.l %%a0,0x01ff9284\n\t" \
            "moveq #0,%%d0\n\t" \
            "move.w #0x0015,%%sr\n\t" \
            OPASM \
            "1:" \
            : \
            : \
            : "a0", "a1", "d0", "cc", "memory"); \
        chk_exception_frame((ID), 0x0020u, 0x0000u, 0x201fu, 0x0015u); \
    } while (0)

    EXPECT_PRIV_OP(0x00100180u, "move.w %%sr,%%d0\n\t");
    EXPECT_PRIV_OP(0x00100540u, "move.w %%d0,%%sr\n\t");
    wr32(RETURN_TEST_BASE + 0xc0u, 0x13579bdfu);
    EXPECT_PRIV_OP(0x001005c0u, "lea 0x01ff99c0,%%a1\n\tmove.w %%sr,(%%a1)\n\t");
    chk32(0x00100600u, rd32(RETURN_TEST_BASE + 0xc0u), 0x13579bdfu);
    EXPECT_PRIV_OP(0x001005e0u, "lea 0x01ff99c0,%%a1\n\tmove.w (%%a1),%%sr\n\t");
    EXPECT_PRIV_OP(0x00100560u, "andi.w #0x00ff,%%sr\n\t");
    EXPECT_PRIV_OP(0x00100580u, "eori.w #0x00ff,%%sr\n\t");
    EXPECT_PRIV_OP(0x001005a0u, "ori.w #0x0700,%%sr\n\t");
    EXPECT_PRIV_OP(0x001001a0u, "movec %%vbr,%%d0\n\t");
    EXPECT_PRIV_OP(0x00100440u, "movec %%msp,%%d0\n\t");
    EXPECT_PRIV_OP(0x00100460u, "movec %%isp,%%d0\n\t");
    EXPECT_PRIV_OP(0x00100480u, "movec %%d0,%%msp\n\t");
    EXPECT_PRIV_OP(0x001004a0u, "movec %%d0,%%isp\n\t");
    EXPECT_PRIV_OP(0x00100500u, "lea 0x01ff9100,%%a1\n\tmoves.l %%d0,(%%a1)\n\t");
    EXPECT_PRIV_OP(0x00100520u, "lea 0x01ff9100,%%a1\n\tmoves.l (%%a1),%%d0\n\t");
    EXPECT_PRIV_OP(0x001001c0u, "lea 0x01ff9e40,%%a1\n\tmove.l %%a1,%%usp\n\t");
    EXPECT_PRIV_OP(0x00100620u, "move.l %%usp,%%a1\n\t");
    EXPECT_PRIV_OP(0x001001e0u, "reset\n\t");
    EXPECT_PRIV_OP(0x00100200u, "stop #0x2000\n\t");
    EXPECT_PRIV_OP(0x00100220u, "rte\n\t");

#undef EXPECT_PRIV_OP

    arm_exception_recovery(0x0024u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0xa700,%%sr\n\t"
        "nop\n"
        "1:"
        :
        :
        : "a0", "cc", "memory");
    chk_exception_frame(0x001000c0u, 0x0024u, 0x2000u, 0xe700u, 0xa700u);

    arm_exception_recovery(0x0028u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        ".word 0xa000\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x001000e0u, 0x0028u, 0x0000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x002cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        ".word 0xf000\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100100u, 0x002cu, 0x0000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        ".word 0xefc8\n\t" /* BFINS with invalid address-register direct EA. */
        ".word 0x0001\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100320u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "lea 0x01ff9100,%%a1\n\t"
        ".word 0xeed9\n\t" /* BFSET with invalid postincrement EA. */
        ".word 0x0001\n"
        "1:"
        :
        :
        : "a0", "a1", "memory");
    chk_exception_frame(0x00100340u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);

#define EXPECT_ILLEGAL_BF_OP(ID, OPWORD) \
    do { \
        arm_exception_recovery(0x0010u); \
        __asm__ volatile( \
            "lea 1f,%%a0\n\t" \
            "move.l %%a0,0x01ff9284\n\t" \
            ".word " #OPWORD "\n\t" \
            ".word 0x0001\n" \
            "1:" \
            : \
            : \
            : "a0", "memory"); \
        chk_exception_frame((ID), 0x0010u, 0x0000u, 0x2000u, 0x2000u); \
    } while (0)

    EXPECT_ILLEGAL_BF_OP(0x00100360u, 0xeac8); /* BFCHG invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x00100380u, 0xecc8); /* BFCLR invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x001003a0u, 0xebc8); /* BFEXTS invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x001003c0u, 0xe9c8); /* BFEXTU invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x001003e0u, 0xedc8); /* BFFFO invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x00100400u, 0xeec8); /* BFSET invalid address-register direct EA. */
    EXPECT_ILLEGAL_BF_OP(0x00100420u, 0xe8c8); /* BFTST invalid address-register direct EA. */

#undef EXPECT_ILLEGAL_BF_OP

    arm_exception_recovery(0x0038u);
    __asm__ volatile(
        "move.l %%sp,%%a1\n\t"
        "lea 0x01ff9400,%%sp\n\t"
        "move.w #0x2700,(%%sp)\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,2(%%sp)\n\t"
        "move.w #0xf038,6(%%sp)\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "rte\n"
        "1:\n\t"
        "move.l %%a1,%%sp"
        :
        :
        : "a0", "a1", "cc", "memory");
    chk_exception_frame(0x00100120u, 0x0038u, 0x0000u, 0x2000u, 0x2000u);
}

#ifdef CORETEST_SIM_IRQ
static void test_interrupt_autovector_directed(void)
{
    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x6cu, (uint32_t)(uintptr_t)_h_recover);

    arm_exception_recovery(0x006cu);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #3,0x01ff9600\n\t"
        "move.w #0x2000,%%sr\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "move.w #0x2700,%%sr\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l #0,0x01ff9600"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110000u, 0x006cu, 0x0000u, 0x2700u, 0x2000u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x6cu, (uint32_t)(uintptr_t)_h_recover);

    arm_exception_recovery(0x006cu);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #3,0x01ff9600\n\t"
        "stop #0x2000\n"
        "1:\n\t"
        "move.w #0x2700,%%sr\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l #0,0x01ff9600"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110020u, 0x006cu, 0x0000u, 0x2700u, 0x2000u);
}
#endif

static void test_alu_shift_bitfield_bcd_directed(void)
{
    uint32_t got;

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.l #-1,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "moveq #1,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "add.l %%d2,%%d0\n\t"
        "addx.l %%d3,%%d1\n\t"
        "move.l %%d0,0x01ff91e0\n\t"
        "move.l %%d1,0x01ff91e4"
        :
        :
        : "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000b0000u, rd32(SCRATCH_BASE + 0xe0), 0u);
    chk32(0x000b0004u, rd32(SCRATCH_BASE + 0xe4), 1u);

    wr32(SCRATCH_BASE + 0xe8, 0u);
    wr32(SCRATCH_BASE + 0xec, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "moveq #1,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "sub.l %%d2,%%d0\n\t"
        "subx.l %%d3,%%d1\n\t"
        "move.l %%d0,0x01ff91e8\n\t"
        "move.l %%d1,0x01ff91ec"
        :
        :
        : "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000b0010u, rd32(SCRATCH_BASE + 0xe8), 0xffffffffu);
    chk32(0x000b0014u, rd32(SCRATCH_BASE + 0xec), 0xffffffffu);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.l #300,%%d0\n\t"
        "mulu.w #200,%%d0\n\t"
        "move.l %%d0,0x01ff91e0\n\t"
        "move.l #60001,%%d0\n\t"
        "divu.w #7,%%d0\n\t"
        "move.l %%d0,0x01ff91e4"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000b0020u, rd32(SCRATCH_BASE + 0xe0), 60000u);
    chk32(0x000b0024u, rd32(SCRATCH_BASE + 0xe4), 0x0004217bu);

    *(volatile uint16_t *)(SCRATCH_BASE + 0xf0) = 0x4001u;
    __asm__ volatile("lsl.w 0x01ff91f0" : : : "cc", "memory");
    chk16(0x000b0030u, rd16(SCRATCH_BASE + 0xf0), 0x8002u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x00u) = 0x8001u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "lsl.w 0x01ffa000\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0100u, rd16(SHIFT_TEST_BASE + 0x00u), 0x0002u);
    chk32(0x000b0104u, got & 0x1fu, 0x11u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x02u) = 0x0001u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "lsr.w 0x01ffa002\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0110u, rd16(SHIFT_TEST_BASE + 0x02u), 0x0000u);
    chk32(0x000b0114u, got & 0x1fu, 0x15u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x04u) = 0x8001u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "asr.w 0x01ffa004\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0120u, rd16(SHIFT_TEST_BASE + 0x04u), 0xc000u);
    chk32(0x000b0124u, got & 0x1fu, 0x19u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x06u) = 0x4001u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "asl.w 0x01ffa006\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0130u, rd16(SHIFT_TEST_BASE + 0x06u), 0x8002u);
    chk32(0x000b0134u, got & 0x1fu, 0x0au);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x08u) = 0x8000u;
    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "roxl.w 0x01ffa008\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0140u, rd16(SHIFT_TEST_BASE + 0x08u), 0x0001u);
    chk32(0x000b0144u, got & 0x1fu, 0x11u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x0au) = 0x0001u;
    __asm__ volatile(
        "move.w #0x10,%%ccr\n\t"
        "roxr.w 0x01ffa00a\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0150u, rd16(SHIFT_TEST_BASE + 0x0au), 0x8000u);
    chk32(0x000b0154u, got & 0x1fu, 0x19u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x0cu) = 0x8001u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "rol.w 0x01ffa00c\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0160u, rd16(SHIFT_TEST_BASE + 0x0cu), 0x0003u);
    chk32(0x000b0164u, got & 0x1fu, 0x01u);

    *(volatile uint16_t *)(SHIFT_TEST_BASE + 0x0eu) = 0x0003u;
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "ror.w 0x01ffa00e\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk16(0x000b0170u, rd16(SHIFT_TEST_BASE + 0x0eu), 0x8001u);
    chk32(0x000b0174u, got & 0x1fu, 0x09u);

    for (uint32_t off = 0x20u; off < 0x60u; off += 4u) {
        wr32(SHIFT_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l #0x40000000,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "asl.l #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa020\n\t"
        "move.l %%d1,0x01ffa024\n\t"
        "move.l #0x12345681,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "lsl.b #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa028\n\t"
        "move.l %%d1,0x01ffa02c\n\t"
        "move.l #0x80000000,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "asr.l #4,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa030\n\t"
        "move.l %%d1,0x01ffa034\n\t"
        "move.l #0x12345678,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "rol.l #8,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa038\n\t"
        "move.l %%d1,0x01ffa03c\n\t"
        "move.l #0x80000000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "roxl.l #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa040\n\t"
        "move.l %%d1,0x01ffa044\n\t"
        "move.l #0x00000001,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "roxr.l #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa048\n\t"
        "move.l %%d1,0x01ffa04c\n\t"
        "move.l #0x000000f0,%%d0\n\t"
        "moveq #4,%%d2\n\t"
        "move.w #0,%%ccr\n\t"
        "lsr.w %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa050\n\t"
        "move.l %%d1,0x01ffa054"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000b0180u, rd32(SHIFT_TEST_BASE + 0x20u), 0x80000000u);
    chk32(0x000b0184u, rd32(SHIFT_TEST_BASE + 0x24u) & 0x1fu, 0x0au);
    chk32(0x000b0188u, rd32(SHIFT_TEST_BASE + 0x28u), 0x12345602u);
    chk32(0x000b018cu, rd32(SHIFT_TEST_BASE + 0x2cu) & 0x1fu, 0x11u);
    chk32(0x000b0190u, rd32(SHIFT_TEST_BASE + 0x30u), 0xf8000000u);
    chk32(0x000b0194u, rd32(SHIFT_TEST_BASE + 0x34u) & 0x1fu, 0x08u);
    chk32(0x000b0198u, rd32(SHIFT_TEST_BASE + 0x38u), 0x34567812u);
    chk32(0x000b019cu, rd32(SHIFT_TEST_BASE + 0x3cu) & 0x1fu, 0x10u);
    chk32(0x000b01a0u, rd32(SHIFT_TEST_BASE + 0x40u), 0x00000001u);
    chk32(0x000b01a4u, rd32(SHIFT_TEST_BASE + 0x44u) & 0x1fu, 0x11u);
    chk32(0x000b01a8u, rd32(SHIFT_TEST_BASE + 0x48u), 0x80000000u);
    chk32(0x000b01acu, rd32(SHIFT_TEST_BASE + 0x4cu) & 0x1fu, 0x19u);
    chk32(0x000b01b0u, rd32(SHIFT_TEST_BASE + 0x50u), 0x0000000fu);
    chk32(0x000b01b4u, rd32(SHIFT_TEST_BASE + 0x54u) & 0x1fu, 0x00u);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.l #0xdeadbeef,%%d0\n\t"
        "bfextu %%d0{#8:#8},%%d1\n\t"
        "move.l %%d1,0x01ff91e0\n\t"
        "move.l #0x12345678,%%d0\n\t"
        "move.l #0x000000ab,%%d1\n\t"
        "bfins %%d1,%%d0{#8:#8}\n\t"
        "move.l %%d0,0x01ff91e4"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000b0040u, rd32(SCRATCH_BASE + 0xe0), 0xadu);
    chk32(0x000b0044u, rd32(SCRATCH_BASE + 0xe4), 0x12ab5678u);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.l #0x00f00000,%%d0\n\t"
        "bfexts %%d0{#8:#8},%%d1\n\t"
        "move.l %%d1,0x01ff91e0\n\t"
        "move.l #0x000f0000,%%d0\n\t"
        "bfffo %%d0{#8:#8},%%d1\n\t"
        "move.l %%d1,0x01ff91e4"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000b0048u, rd32(SCRATCH_BASE + 0xe0), 0xfffffff0u);
    chk32(0x000b004cu, rd32(SCRATCH_BASE + 0xe4), 12u);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.w #0x0205,%%d0\n\t"
        "pack %%d0,%%d0,#0\n\t"
        "move.l %%d0,0x01ff91e0\n\t"
        "move.b #0x25,%%d0\n\t"
        "unpk %%d0,%%d0,#0\n\t"
        "move.l %%d0,0x01ff91e4"
        :
        :
        : "d0", "cc", "memory");
    chk8(0x000b0050u, rd32(SCRATCH_BASE + 0xe0), 0x25u);
    chk16(0x000b0054u, rd32(SCRATCH_BASE + 0xe4), 0x0205u);

    wr32(SCRATCH_BASE + 0xe0, 0u);
    wr32(SCRATCH_BASE + 0xe4, 0u);
    __asm__ volatile(
        "move.b #0x45,%%d0\n\t"
        "move.b #0x55,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "abcd %%d0,%%d1\n\t"
        "move.b %%d1,0x01ff91e0\n\t"
        "move.b #0x01,%%d0\n\t"
        "move.b #0x00,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "sbcd %%d0,%%d1\n\t"
        "move.b %%d1,0x01ff91e4"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk8(0x000b0060u, rd8(SCRATCH_BASE + 0xe0), 0x00u);
    chk8(0x000b0064u, rd8(SCRATCH_BASE + 0xe4), 0x99u);
}

static void test_condition_codes_directed(void)
{
    uint32_t got;

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.b #0x7f,%%d0\n\t"
        "add.b #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0000u, got & 0x1fu, 0x0au);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "move.b #0xff,%%d0\n\t"
        "add.b #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0001u, got & 0x1fu, 0x15u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "subq.w #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0002u, got & 0x1fu, 0x19u);

    __asm__ volatile(
        "moveq #5,%%d0\n\t"
        "moveq #5,%%d1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "cmp.l %%d1,%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,%0"
        : "=d"(got)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x000c0003u, got & 0x1fu, 0x14u);

    for (uint32_t off = 0; off < 0x40u; off += 4u) {
        wr32(COND_TEST_BASE + off, 0xaaaaaaaau);
    }

    __asm__ volatile(
        "lea 0x01ffa100,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "st (%%a0)+\n\t"
        "sf (%%a0)+\n\t"
        "shi (%%a0)+\n\t"
        "sls (%%a0)+\n\t"
        "scc (%%a0)+\n\t"
        "scs (%%a0)+\n\t"
        "sne (%%a0)+\n\t"
        "seq (%%a0)+\n\t"
        "svc (%%a0)+\n\t"
        "svs (%%a0)+\n\t"
        "spl (%%a0)+\n\t"
        "smi (%%a0)+\n\t"
        "sge (%%a0)+\n\t"
        "slt (%%a0)+\n\t"
        "sgt (%%a0)+\n\t"
        "sle (%%a0)+"
        :
        :
        : "a0", "cc", "memory");
    chk32(0x000c0100u, rd32(COND_TEST_BASE + 0x00u), 0xff00ff00u);
    chk32(0x000c0104u, rd32(COND_TEST_BASE + 0x04u), 0xff00ff00u);
    chk32(0x000c0108u, rd32(COND_TEST_BASE + 0x08u), 0xff00ff00u);
    chk32(0x000c010cu, rd32(COND_TEST_BASE + 0x0cu), 0xff00ff00u);

    __asm__ volatile(
        "lea 0x01ffa120,%%a0\n\t"
        "move.w #0x0f,%%ccr\n\t"
        "st (%%a0)+\n\t"
        "sf (%%a0)+\n\t"
        "shi (%%a0)+\n\t"
        "sls (%%a0)+\n\t"
        "scc (%%a0)+\n\t"
        "scs (%%a0)+\n\t"
        "sne (%%a0)+\n\t"
        "seq (%%a0)+\n\t"
        "svc (%%a0)+\n\t"
        "svs (%%a0)+\n\t"
        "spl (%%a0)+\n\t"
        "smi (%%a0)+\n\t"
        "sge (%%a0)+\n\t"
        "slt (%%a0)+\n\t"
        "sgt (%%a0)+\n\t"
        "sle (%%a0)+"
        :
        :
        : "a0", "cc", "memory");
    chk32(0x000c0120u, rd32(COND_TEST_BASE + 0x20u), 0xff0000ffu);
    chk32(0x000c0124u, rd32(COND_TEST_BASE + 0x24u), 0x00ff00ffu);
    chk32(0x000c0128u, rd32(COND_TEST_BASE + 0x28u), 0x00ff00ffu);
    chk32(0x000c012cu, rd32(COND_TEST_BASE + 0x2cu), 0xff0000ffu);

    for (uint32_t off = 0x50u; off <= 0x60u; off += 4u) {
        wr32(COND_TEST_BASE + off, 0xaaaaaaaau);
    }
    __asm__ volatile(
        "lea 0x01ffa160,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "st -(%%a0)\n\t"
        "sf -(%%a0)\n\t"
        "seq -(%%a0)\n\t"
        "sne -(%%a0)\n\t"
        "move.l %%a0,%0"
        : "=d"(got)
        :
        : "a0", "cc", "memory");
    chk32(0x000c0130u, got, COND_TEST_BASE + 0x5cu);
    chk32(0x000c0134u, rd32(COND_TEST_BASE + 0x5cu), 0xff0000ffu);
}

static void test_condition_consumers_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #5,%%d1\n\t"
        "cmp.l #5,%%d1\n\t"
        "beq 1f\n\t"
        "bset #0,%%d0\n"
        "1:\n\t"
        "bne 2f\n\t"
        "bra 3f\n"
        "2:\n\t"
        "bset #1,%%d0\n"
        "3:\n\t"
        "bgt 4f\n\t"
        "bra 5f\n"
        "4:\n\t"
        "bset #2,%%d0\n"
        "5:\n\t"
        "ble 6f\n\t"
        "bset #3,%%d0\n"
        "6:\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0200u, got0, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #7,%%d1\n\t"
        "cmp.l #5,%%d1\n\t"
        "bhi 1f\n\t"
        "bset #0,%%d0\n"
        "1:\n\t"
        "bls 2f\n\t"
        "bra 3f\n"
        "2:\n\t"
        "bset #1,%%d0\n"
        "3:\n\t"
        "bgt 4f\n\t"
        "bset #2,%%d0\n"
        "4:\n\t"
        "ble 5f\n\t"
        "bra 6f\n"
        "5:\n\t"
        "bset #3,%%d0\n"
        "6:\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0204u, got0, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #3,%%d1\n\t"
        "cmp.l #5,%%d1\n\t"
        "bcs 1f\n\t"
        "bset #0,%%d0\n"
        "1:\n\t"
        "bcc 2f\n\t"
        "bra 3f\n"
        "2:\n\t"
        "bset #1,%%d0\n"
        "3:\n\t"
        "blt 4f\n\t"
        "bset #2,%%d0\n"
        "4:\n\t"
        "bge 5f\n\t"
        "bra 6f\n"
        "5:\n\t"
        "bset #3,%%d0\n"
        "6:\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0208u, got0, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l #0x7fffffff,%%d1\n\t"
        "cmp.l #-1,%%d1\n\t"
        "bgt 1f\n\t"
        "bset #0,%%d0\n"
        "1:\n\t"
        "blt 2f\n\t"
        "bra 3f\n"
        "2:\n\t"
        "bset #1,%%d0\n"
        "3:\n\t"
        "bvs 4f\n\t"
        "bset #2,%%d0\n"
        "4:\n\t"
        "bpl 5f\n\t"
        "bra 6f\n"
        "5:\n\t"
        "bset #3,%%d0\n"
        "6:\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc");
    chk32(0x000c020cu, got0, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #2,%%d1\n\t"
        "suba.l %%a0,%%a0\n\t"
        "cmp.l %%d0,%%d0\n"
        "1:\n\t"
        "addq.l #1,%%a0\n\t"
        "dbne %%d1,1b\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x000c0210u, got0, 3u);
    chk32(0x000c0214u, got1 & 0xffffu, 0xffffu);

    __asm__ volatile(
        "move.l #0x12340001,%%d0\n"
        "1:\n\t"
        "dbra %%d0,1b\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc");
    chk32(0x000c0218u, got0, 0x1234ffffu);

    __asm__ volatile(
        "moveq #3,%%d0\n"
        "1:\n\t"
        "dbra %%d0,1b\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "d0", "cc");
    chk32(0x000c021cu, got0, 0x0000ffffu);

    __asm__ volatile(
        "moveq #3,%%d0\n\t"
        "cmp.l %%d1,%%d1\n\t"
        "dbeq %%d0,1f\n\t"
        "move.l %%d0,%0\n\t"
        "bra 2f\n"
        "1:\n\t"
        "moveq #-1,%%d0\n\t"
        "move.l %%d0,%0\n"
        "2:"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc");
    chk32(0x000c0220u, got0, 3u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #9,%%d4\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "seq %%d0\n\t"
        "sne %%d1\n\t"
        "dbne %%d4,1f\n\t"
        "moveq #0x33,%%d2\n"
        "1:\n\t"
        "move.l %%d0,0x01ffa140\n\t"
        "move.l %%d1,0x01ffa144\n\t"
        "move.l %%d2,0x01ffa148\n\t"
        "move.l %%d4,0x01ffa14c"
        :
        :
        : "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000c0224u, rd32(COND_TEST_BASE + 0x40u) & 0xffu, 0xffu);
    chk32(0x000c0228u, rd32(COND_TEST_BASE + 0x44u) & 0xffu, 0x00u);
    chk32(0x000c022cu, rd32(COND_TEST_BASE + 0x48u), 0x00u);
    chk32(0x000c0230u, rd32(COND_TEST_BASE + 0x4cu) & 0xffffu, 0x0008u);
}

static void test_bitops_directed(void)
{
    wr32(BITOP_TEST_BASE + 0x00u, 0u);
    wr32(BITOP_TEST_BASE + 0x04u, 0u);
    wr32(BITOP_TEST_BASE + 0x08u, 0u);
    wr32(BITOP_TEST_BASE + 0x0cu, 0u);
    wr32(BITOP_TEST_BASE + 0x10u, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "bset #31,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa300\n\t"
        "move.l %%d1,0x01ffa304\n\t"
        "bchg #31,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa308\n\t"
        "move.l %%d1,0x01ffa30c\n\t"
        "btst #5,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa310"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000c0200u, rd32(BITOP_TEST_BASE + 0x00u), 0x80000000u);
    chk32(0x000c0204u, rd32(BITOP_TEST_BASE + 0x04u) & 0x04u, 0x04u);
    chk32(0x000c0208u, rd32(BITOP_TEST_BASE + 0x08u), 0x00000000u);
    chk32(0x000c020cu, rd32(BITOP_TEST_BASE + 0x0cu) & 0x04u, 0x00u);
    chk32(0x000c0210u, rd32(BITOP_TEST_BASE + 0x10u) & 0x04u, 0x04u);

    wr32(BITOP_TEST_BASE + 0x20u, 0u);
    wr32(BITOP_TEST_BASE + 0x30u, 0u);
    wr32(BITOP_TEST_BASE + 0x34u, 0u);
    wr32(BITOP_TEST_BASE + 0x38u, 0u);
    wr32(BITOP_TEST_BASE + 0x3cu, 0u);
    wr32(BITOP_TEST_BASE + 0x40u, 0u);
    __asm__ volatile(
        "moveq #7,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "bset %%d0,0x01ffa320\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa330\n\t"
        "bclr %%d0,0x01ffa320\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa334\n\t"
        "moveq #1,%%d0\n\t"
        "bchg %%d0,0x01ffa323\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa338\n\t"
        "btst #1,0x01ffa323\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa33c\n\t"
        "bset #10,0x01ffa322\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa340"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000c0220u, rd32(BITOP_TEST_BASE + 0x20u), 0x00000402u);
    chk32(0x000c0230u, rd32(BITOP_TEST_BASE + 0x30u) & 0x04u, 0x04u);
    chk32(0x000c0234u, rd32(BITOP_TEST_BASE + 0x34u) & 0x04u, 0x00u);
    chk32(0x000c0238u, rd32(BITOP_TEST_BASE + 0x38u) & 0x04u, 0x04u);
    chk32(0x000c023cu, rd32(BITOP_TEST_BASE + 0x3cu) & 0x04u, 0x00u);
    chk32(0x000c0240u, rd32(BITOP_TEST_BASE + 0x40u) & 0x04u, 0x04u);

    for (uint32_t off = 0x50u; off <= 0x74u; off += 4u) {
        wr32(BITOP_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l #37,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "bset %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,0x01ffa350\n\t"
        "move.l %%d2,0x01ffa354\n\t"
        "move.l #69,%%d0\n\t"
        "btst %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ffa358\n\t"
        "move.l #37,%%d0\n\t"
        "bclr %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,0x01ffa35c\n\t"
        "move.l %%d2,0x01ffa360\n\t"
        "move.l #13,%%d0\n\t"
        "bset %%d0,0x01ffa364\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa368\n\t"
        "move.l 0x01ffa364,0x01ffa374\n\t"
        "move.l #45,%%d0\n\t"
        "btst %%d0,0x01ffa364\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa36c\n\t"
        "move.l #21,%%d0\n\t"
        "bclr %%d0,0x01ffa364\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa370"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000c0300u, rd32(BITOP_TEST_BASE + 0x50u), 0x00000020u);
    chk32(0x000c0304u, rd32(BITOP_TEST_BASE + 0x54u) & 0x04u, 0x04u);
    chk32(0x000c0308u, rd32(BITOP_TEST_BASE + 0x58u) & 0x04u, 0x00u);
    chk32(0x000c030cu, rd32(BITOP_TEST_BASE + 0x5cu), 0x00000000u);
    chk32(0x000c0310u, rd32(BITOP_TEST_BASE + 0x60u) & 0x04u, 0x00u);
    chk32(0x000c0314u, rd32(BITOP_TEST_BASE + 0x74u), 0x20000000u);
    chk32(0x000c0318u, rd32(BITOP_TEST_BASE + 0x68u) & 0x04u, 0x04u);
    chk32(0x000c031cu, rd32(BITOP_TEST_BASE + 0x6cu) & 0x04u, 0x00u);
    chk32(0x000c0320u, rd32(BITOP_TEST_BASE + 0x64u), 0x00000000u);
    chk32(0x000c0324u, rd32(BITOP_TEST_BASE + 0x70u) & 0x04u, 0x00u);
}

static void test_signed_mul_div_directed(void)
{
    wr32(SCRATCH_BASE + 0x120, 0u);
    wr32(SCRATCH_BASE + 0x124, 0u);
    wr32(SCRATCH_BASE + 0x128, 0u);
    wr32(SCRATCH_BASE + 0x12c, 0u);
    wr32(SCRATCH_BASE + 0x130, 0u);
    wr32(SCRATCH_BASE + 0x134, 0u);
    wr32(SCRATCH_BASE + 0x138, 0u);
    wr32(SCRATCH_BASE + 0x13c, 0u);
    wr32(SCRATCH_BASE + 0x140, 0u);
    wr32(SCRATCH_BASE + 0x144, 0u);
    wr32(SCRATCH_BASE + 0x148, 0u);
    wr32(SCRATCH_BASE + 0x14c, 0u);
    wr32(SCRATCH_BASE + 0x150, 0u);
    wr32(SCRATCH_BASE + 0x154, 0u);
    wr32(SCRATCH_BASE + 0x158, 0u);
    wr32(SCRATCH_BASE + 0x15c, 0u);
    __asm__ volatile(
        "move.l #-1234,%%d0\n\t"
        "muls.w #45,%%d0\n\t"
        "move.l %%d0,0x01ff9220\n\t"
        "move.l #-60000,%%d0\n\t"
        "divs.w #-7,%%d0\n\t"
        "move.l %%d0,0x01ff9224\n\t"
        "move.l #1000,%%d0\n\t"
        "divu.l #7,%%d0\n\t"
        "move.l %%d0,0x01ff9228\n\t"
        "move.l #-1000,%%d0\n\t"
        "move.l #7,%%d4\n\t"
        "divs.l %%d4,%%d0\n\t"
        "move.l %%d0,0x01ff922c\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "moveq #3,%%d4\n\t"
        "divu.l %%d4,%%d1:%%d0\n\t"
        "move.l %%d0,0x01ff9230\n\t"
        "move.l %%d1,0x01ff9234\n\t"
        "move.l #-1000,%%d0\n\t"
        "moveq #-1,%%d1\n\t"
        "moveq #7,%%d4\n\t"
        "divs.l %%d4,%%d1:%%d0\n\t"
        "move.l %%d0,0x01ff9238\n\t"
        "move.l %%d1,0x01ff923c\n\t"
        "move.l #0x00010000,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "divu.w #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9240\n\t"
        "move.l %%d1,0x01ff9244\n\t"
        "move.l #0x00008000,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "divs.w #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9248\n\t"
        "move.l %%d1,0x01ff924c\n\t"
        "move.l #-32768,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "divs.w #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9250\n\t"
        "move.l %%d1,0x01ff9254\n\t"
        "move.l #-32769,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "divs.w #1,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9258\n\t"
        "move.l %%d1,0x01ff925c"
        :
        :
        : "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d0000u, rd32(SCRATCH_BASE + 0x120), 0xffff2716u);
    chk32(0x000d0004u, rd32(SCRATCH_BASE + 0x124), 0xfffd217bu);
    chk32(0x000d0010u, rd32(SCRATCH_BASE + 0x128), 0x0000008eu);
    chk32(0x000d0014u, rd32(SCRATCH_BASE + 0x12c), 0xffffff72u);
    chk32(0x000d0018u, rd32(SCRATCH_BASE + 0x130), 0x55555555u);
    chk32(0x000d001cu, rd32(SCRATCH_BASE + 0x134), 0x00000001u);
    chk32(0x000d0020u, rd32(SCRATCH_BASE + 0x138), 0xffffff72u);
    chk32(0x000d0024u, rd32(SCRATCH_BASE + 0x13c), 0xfffffffau);
    chk32(0x000d0030u, rd32(SCRATCH_BASE + 0x140), 0x00010000u);
    chk32(0x000d0034u, rd32(SCRATCH_BASE + 0x144) & 0x02u, 0x02u);
    chk32(0x000d0038u, rd32(SCRATCH_BASE + 0x148), 0x00008000u);
    chk32(0x000d003cu, rd32(SCRATCH_BASE + 0x14c) & 0x02u, 0x02u);
    chk32(0x000d0040u, rd32(SCRATCH_BASE + 0x150), 0x00008000u);
    chk32(0x000d0044u, rd32(SCRATCH_BASE + 0x154) & 0x02u, 0x00u);
    chk32(0x000d0048u, rd32(SCRATCH_BASE + 0x158), 0xffff7fffu);
    chk32(0x000d004cu, rd32(SCRATCH_BASE + 0x15c) & 0x02u, 0x02u);
}

static void test_mul_div_memory_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0x00u) = 0xfffeu;
    __asm__ volatile(
        "lea 0x01ffa900,%%a0\n\t"
        "moveq #3,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "mulu.w (%%a0)+,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000d0100u, got0, MUL_DIV_TEST_BASE + 0x02u);
    chk32(0x000d0104u, got1, 0x0002fffau);
    chk32(0x000d0108u, got2 & 0x1fu, 0x10u);

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0x10u) = 0xfffdu;
    __asm__ volatile(
        "lea 0x01ffa910,%%a0\n\t"
        "moveq #7,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "muls.w (%%a0)+,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1\n\t"
        "move.l %%d1,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000d0110u, got0, MUL_DIV_TEST_BASE + 0x12u);
    chk32(0x000d0114u, got1, 0xffffffebu);
    chk32(0x000d0118u, got2 & 0x1fu, 0x18u);

    wr32(MUL_DIV_TEST_BASE + 0x20u, 0x00030004u);
    __asm__ volatile(
        "lea 0x01ffa920,%%a0\n\t"
        "move.l #0x00010002,%%d0\n\t"
        "move.l #0xdeadbeef,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "mulu.l (%%a0),%%d1:%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000d0120u, got0, 0x000a0008u);
    chk32(0x000d0124u, got1, 0x00000003u);
    chk32(0x000d0128u, got2 & 0x1fu, 0x10u);
    chk32(0x000d012cu, got3, MUL_DIV_TEST_BASE + 0x20u);

    wr32(MUL_DIV_TEST_BASE + 0x30u, 30000u);
    __asm__ volatile(
        "lea 0x01ffa930,%%a0\n\t"
        "move.l #-200000,%%d2\n\t"
        "move.l #0xdeadbeef,%%d3\n\t"
        "move.w #0x10,%%ccr\n\t"
        "muls.l (%%a0),%%d3:%%d2\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d2,%0\n\t"
        "move.l %%d3,%1\n\t"
        "move.l %%d4,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000d0130u, got0, 0x9a5f4400u);
    chk32(0x000d0134u, got1, 0xfffffffeu);
    chk32(0x000d0138u, got2 & 0x1fu, 0x18u);
    chk32(0x000d013cu, got3, MUL_DIV_TEST_BASE + 0x30u);

    wr32(MUL_DIV_TEST_BASE + 0x40u, 17u);
    __asm__ volatile(
        "lea 0x01ffa940,%%a0\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.l (%%a0),%%d1:%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000d0140u, got0, 0x0f0f0f0fu);
    chk32(0x000d0144u, got1, 0x00000001u);
    chk32(0x000d0148u, got2 & 0x1fu, 0x10u);
    chk32(0x000d014cu, got3, MUL_DIV_TEST_BASE + 0x40u);

    wr32(MUL_DIV_TEST_BASE + 0x50u, 13u);
    __asm__ volatile(
        "lea 0x01ffa950,%%a0\n\t"
        "move.l #0xffffff9c,%%d2\n\t"
        "move.l #0xfffffffe,%%d3\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.l (%%a0),%%d3:%%d2\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d2,%0\n\t"
        "move.l %%d3,%1\n\t"
        "move.l %%d4,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000d0150u, got0, 0xec4ec4e5u);
    chk32(0x000d0154u, got1, 0xfffffffbu);
    chk32(0x000d0158u, got2 & 0x1fu, 0x18u);
    chk32(0x000d015cu, got3, MUL_DIV_TEST_BASE + 0x50u);

    wr32(MUL_DIV_TEST_BASE + 0x60u, 1u);
    __asm__ volatile(
        "lea 0x01ffa960,%%a0\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #2,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.l (%%a0),%%d1:%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000d0160u, got0, 0x00000000u);
    chk32(0x000d0164u, got1, 0x00000002u);
    chk32(0x000d0168u, got2 & 0x02u, 0x02u);
    chk32(0x000d016cu, got3, MUL_DIV_TEST_BASE + 0x60u);
}

static void test_memory_bitfield_directed(void)
{
    wr32(SCRATCH_BASE + 0x130, 0x12345678u);
    wr32(SCRATCH_BASE + 0x134, 0u);
    __asm__ volatile(
        "bfextu 0x01ff9230{#4:#12},%%d0\n\t"
        "move.l %%d0,0x01ff9234"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0000u, rd32(SCRATCH_BASE + 0x134), 0x234u);

    wr32(SCRATCH_BASE + 0x130, 0x12345678u);
    __asm__ volatile(
        "move.l #0xab,%%d0\n\t"
        "bfins %%d0,0x01ff9230{#12:#8}"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0001u, rd32(SCRATCH_BASE + 0x130), 0x123ab678u);

    wr32(SCRATCH_BASE + 0x130, 0x12345678u);
    __asm__ volatile("bfchg 0x01ff9230{#8:#4}" : : : "cc", "memory");
    chk32(0x000e0002u, rd32(SCRATCH_BASE + 0x130), 0x12c45678u);

    wr32(SCRATCH_BASE + 0x130, 0x12345678u);
    __asm__ volatile("bfclr 0x01ff9230{#12:#4}" : : : "cc", "memory");
    chk32(0x000e0003u, rd32(SCRATCH_BASE + 0x130), 0x12305678u);

    wr32(SCRATCH_BASE + 0x130, 0x12345678u);
    __asm__ volatile("bfset 0x01ff9230{#16:#8}" : : : "cc", "memory");
    chk32(0x000e0004u, rd32(SCRATCH_BASE + 0x130), 0x1234ff78u);
}

static void test_memory_bitfield_extended_directed(void)
{
    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "bfextu 0x01ff9d00{#28:#12},%%d0\n\t"
        "move.l %%d0,0x01ff9d08"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0050u, rd32(BITFIELD_TEST_BASE + 0x08), 0x789u);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "moveq #20,%%d0\n\t"
        "moveq #9,%%d1\n\t"
        "bfextu 0x01ff9d00{%%d0:%%d1},%%d2\n\t"
        "move.l %%d2,0x01ff9d08"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000e0054u, rd32(BITFIELD_TEST_BASE + 0x08), 0x0acu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "move.l #0xabc,%%d0\n\t"
        "bfins %%d0,0x01ff9d00{#28:#12}"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0058u, rd32(BITFIELD_TEST_BASE + 0x00), 0x0123456au);
    chk32(0x000e005cu, rd32(BITFIELD_TEST_BASE + 0x04), 0xbcabcdefu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0xffffffffu);
    wr32(BITFIELD_TEST_BASE + 0x04, 0xffffffffu);
    __asm__ volatile("bfclr 0x01ff9d00{#31:#3}" : : : "cc", "memory");
    chk32(0x000e0060u, rd32(BITFIELD_TEST_BASE + 0x00), 0xfffffffeu);
    chk32(0x000e0064u, rd32(BITFIELD_TEST_BASE + 0x04), 0x3fffffffu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x00000f00u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0u);
    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "bfexts 0x01ff9d00{#20:#8},%%d0\n\t"
        "move.l %%d0,0x01ff9d08"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0068u, rd32(BITFIELD_TEST_BASE + 0x08), 0xfffffff0u);

    wr32(BITFIELD_TEST_BASE + 0x3c, 0x000000a5u);
    wr32(BITFIELD_TEST_BASE + 0x40, 0x5a000000u);
    wr32(BITFIELD_TEST_BASE + 0x48, 0u);
    __asm__ volatile(
        "moveq #-4,%%d0\n\t"
        "bfextu 0x01ff9d40{%%d0:#8},%%d1\n\t"
        "move.l %%d1,0x01ff9d48"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000e006cu, rd32(BITFIELD_TEST_BASE + 0x48), 0x55u);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x00000000u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x10000000u);
    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "bfffo 0x01ff9d00{#28:#8},%%d0\n\t"
        "move.l %%d0,0x01ff9d08"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e0070u, rd32(BITFIELD_TEST_BASE + 0x08), 35u);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "moveq #12,%%d1\n\t"
        "move.l #0xabc,%%d2\n\t"
        "bfins %%d2,0x01ff9d00{#28:%%d1}"
        :
        :
        : "d1", "d2", "cc", "memory");
    chk32(0x000e0074u, rd32(BITFIELD_TEST_BASE + 0x00), 0x0123456au);
    chk32(0x000e0078u, rd32(BITFIELD_TEST_BASE + 0x04), 0xbcabcdefu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "moveq #28,%%d0\n\t"
        "moveq #12,%%d1\n\t"
        "move.l #0xabc,%%d2\n\t"
        "bfins %%d2,0x01ff9d00{%%d0:%%d1}"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000e007cu, rd32(BITFIELD_TEST_BASE + 0x00), 0x0123456au);
    chk32(0x000e0080u, rd32(BITFIELD_TEST_BASE + 0x04), 0xbcabcdefu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "moveq #12,%%d1\n\t"
        "bfclr 0x01ff9d00{#28:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x000e0084u, rd32(BITFIELD_TEST_BASE + 0x00), 0x01234560u);
    chk32(0x000e0088u, rd32(BITFIELD_TEST_BASE + 0x04), 0x00abcdefu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "moveq #12,%%d1\n\t"
        "bfset 0x01ff9d00{#28:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x000e008cu, rd32(BITFIELD_TEST_BASE + 0x00), 0x0123456fu);
    chk32(0x000e0090u, rd32(BITFIELD_TEST_BASE + 0x04), 0xffabcdefu);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x89abcdefu);
    __asm__ volatile(
        "moveq #12,%%d1\n\t"
        "bfchg 0x01ff9d00{#28:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x000e0094u, rd32(BITFIELD_TEST_BASE + 0x00), 0x01234568u);
    chk32(0x000e0098u, rd32(BITFIELD_TEST_BASE + 0x04), 0x76abcdefu);

    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "move.l #0x12345678,%%d0\n\t"
        "moveq #12,%%d1\n\t"
        "move.l #0xabc,%%d2\n\t"
        "bfins %%d2,%%d0{#16:%%d1}\n\t"
        "move.l %%d0,0x01ff9d08"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000e009cu, rd32(BITFIELD_TEST_BASE + 0x08), 0x1234abc8u);

    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "moveq #16,%%d0\n\t"
        "moveq #12,%%d1\n\t"
        "move.l #0xabc,%%d2\n\t"
        "move.l #0x12345678,%%d3\n\t"
        "bfins %%d2,%%d3{%%d0:%%d1}\n\t"
        "move.l %%d3,0x01ff9d08"
        :
        :
        : "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000e00a0u, rd32(BITFIELD_TEST_BASE + 0x08), 0x1234abc8u);
}

static void test_movep_tas_cas_directed(void)
{
    uint32_t got;

    wr32(SCRATCH_BASE + 0x140, 0u);
    wr32(SCRATCH_BASE + 0x144, 0u);
    wr32(SCRATCH_BASE + 0x148, 0u);
    __asm__ volatile(
        "lea 0x01ff9240,%%a0\n\t"
        "move.l #0x11223344,%%d0\n\t"
        "movep.l %%d0,0(%%a0)\n\t"
        "moveq #0,%%d1\n\t"
        "movep.l 0(%%a0),%%d1\n\t"
        "move.l %%d1,0x01ff9248"
        :
        :
        : "a0", "d0", "d1", "memory");
    chk32(0x000f0000u, rd32(SCRATCH_BASE + 0x140), 0x11002200u);
    chk32(0x000f0004u, rd32(SCRATCH_BASE + 0x144), 0x33004400u);
    chk32(0x000f0008u, rd32(SCRATCH_BASE + 0x148), 0x11223344u);

    wr32(SCRATCH_BASE + 0x150, 0u);
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "move.b #0x05,0x01ff9250\n\t"
        "tas.b 0x01ff9250\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "cc", "memory");
    chk8(0x000f0010u, rd8(SCRATCH_BASE + 0x150), 0x85u);
    chk32(0x000f0011u, got & 0x1fu, 0u);

    wr32(SCRATCH_BASE + 0x150, 0u);
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "clr.b 0x01ff9250\n\t"
        "tas.b 0x01ff9250\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=d"(got)
        :
        : "d0", "cc", "memory");
    chk8(0x000f0012u, rd8(SCRATCH_BASE + 0x150), 0x80u);
    chk32(0x000f0013u, got & 0x1fu, 0x04u);

    wr32(SCRATCH_BASE + 0x160, 0x11111111u);
    wr32(SCRATCH_BASE + 0x168, 0u);
    __asm__ volatile(
        "lea 0x01ff9260,%%a0\n\t"
        "move.l #0x11111111,%%d0\n\t"
        "move.l #0x22222222,%%d1\n\t"
        "cas.l %%d0,%%d1,(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff9268\n\t"
        "move.l %%d0,0x01ff9264\n\t"
        "move.l %%d2,%0"
        : "=d"(got)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0020u, rd32(SCRATCH_BASE + 0x160), 0x22222222u);
    chk32(0x000f0024u, rd32(SCRATCH_BASE + 0x164), 0x11111111u);
    chk32(0x000f0028u, rd32(SCRATCH_BASE + 0x168) & 0x04u, 0x04u);
    chk32(0x000f002cu, got & 0x04u, 0x04u);

    wr32(SCRATCH_BASE + 0x160, 0x33333333u);
    wr32(SCRATCH_BASE + 0x164, 0u);
    wr32(SCRATCH_BASE + 0x168, 0u);
    __asm__ volatile(
        "lea 0x01ff9260,%%a0\n\t"
        "move.l #0x11111111,%%d0\n\t"
        "move.l #0x22222222,%%d1\n\t"
        "cas.l %%d0,%%d1,(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff9268\n\t"
        "move.l %%d0,0x01ff9264\n\t"
        "move.l %%d2,%0"
        : "=d"(got)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0030u, rd32(SCRATCH_BASE + 0x160), 0x33333333u);
    chk32(0x000f0034u, rd32(SCRATCH_BASE + 0x164), 0x33333333u);
    chk32(0x000f0038u, rd32(SCRATCH_BASE + 0x168) & 0x04u, 0u);
    chk32(0x000f003cu, got & 0x04u, 0u);

    wr32(SCRATCH_BASE + 0x160, 0x44444444u);
    wr32(SCRATCH_BASE + 0x164, 0u);
    wr32(SCRATCH_BASE + 0x168, 0u);
    __asm__ volatile(
        "lea 0x01ff9260,%%a0\n\t"
        "move.l #0x44444444,%%d0\n\t"
        "move.l #0x55555555,%%d1\n\t"
        "cas.l %%d0,%%d1,(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9264\n\t"
        "move.l %%d2,%%d4\n\t"
        "move.l %%d4,0x01ff9268"
        :
        :
        : "a0", "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000f0040u, rd32(SCRATCH_BASE + 0x160), 0x55555555u);
    chk32(0x000f0044u, rd32(SCRATCH_BASE + 0x164), 0x44444444u);
    chk32(0x000f0048u, rd32(SCRATCH_BASE + 0x168) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0x30u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0x34u, 0u);
    wr32(CAS2_TEST_BASE + 0x38u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa0033,%%d0\n\t"
        "move.l #0xbbbb0077,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.b %%d0,%%d1,0x01ff9872\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9874\n\t"
        "move.l %%d2,0x01ff9878"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0070u, rd32(CAS2_TEST_BASE + 0x30u), 0x11227744u);
    chk32(0x000f0074u, rd32(CAS2_TEST_BASE + 0x34u), 0xaaaa0033u);
    chk32(0x000f0078u, rd32(CAS2_TEST_BASE + 0x38u) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0x30u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0x34u, 0u);
    wr32(CAS2_TEST_BASE + 0x38u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa0055,%%d0\n\t"
        "move.l #0xbbbb0077,%%d1\n\t"
        "move.w #4,%%ccr\n\t"
        "cas.b %%d0,%%d1,0x01ff9872\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9874\n\t"
        "move.l %%d2,0x01ff9878"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0080u, rd32(CAS2_TEST_BASE + 0x30u), 0x11223344u);
    chk32(0x000f0084u, rd32(CAS2_TEST_BASE + 0x34u), 0xaaaa0033u);
    chk32(0x000f0088u, rd32(CAS2_TEST_BASE + 0x38u) & 0x04u, 0u);

    wr32(CAS2_TEST_BASE + 0x30u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0x34u, 0u);
    wr32(CAS2_TEST_BASE + 0x38u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa1122,%%d0\n\t"
        "move.l #0xbbbb7788,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.w %%d0,%%d1,0x01ff9870\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9874\n\t"
        "move.l %%d2,0x01ff9878"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0090u, rd32(CAS2_TEST_BASE + 0x30u), 0x77883344u);
    chk32(0x000f0094u, rd32(CAS2_TEST_BASE + 0x34u), 0xaaaa1122u);
    chk32(0x000f0098u, rd32(CAS2_TEST_BASE + 0x38u) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0x30u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0x34u, 0u);
    wr32(CAS2_TEST_BASE + 0x38u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa5566,%%d0\n\t"
        "move.l #0xbbbb7788,%%d1\n\t"
        "move.w #4,%%ccr\n\t"
        "cas.w %%d0,%%d1,0x01ff9870\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9874\n\t"
        "move.l %%d2,0x01ff9878"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f00a0u, rd32(CAS2_TEST_BASE + 0x30u), 0x11223344u);
    chk32(0x000f00a4u, rd32(CAS2_TEST_BASE + 0x34u), 0xaaaa1122u);
    chk32(0x000f00a8u, rd32(CAS2_TEST_BASE + 0x38u) & 0x04u, 0u);
}

static void test_movep_displacement_directed(void)
{
    wr32(MOVEP_TEST_BASE + 0x00, 0u);
    wr32(MOVEP_TEST_BASE + 0x04, 0u);
    wr32(MOVEP_TEST_BASE + 0x08, 0u);
    wr32(MOVEP_TEST_BASE + 0x0c, 0u);
    __asm__ volatile(
        "lea 0x01ff9c00,%%a0\n\t"
        "move.l #0xffffa1b2,%%d0\n\t"
        "movep.w %%d0,2(%%a0)\n\t"
        "moveq #0,%%d1\n\t"
        "movep.w 2(%%a0),%%d1\n\t"
        "move.l %%d1,0x01ff9c08\n\t"
        "move.l %%a0,0x01ff9c0c"
        :
        :
        : "a0", "d0", "d1", "memory");
    chk32(0x000f0050u, rd32(MOVEP_TEST_BASE + 0x00), 0x0000a100u);
    chk32(0x000f0054u, rd32(MOVEP_TEST_BASE + 0x04), 0xb2000000u);
    chk32(0x000f0058u, rd32(MOVEP_TEST_BASE + 0x08), 0x0000a1b2u);
    chk32(0x000f005cu, rd32(MOVEP_TEST_BASE + 0x0c), MOVEP_TEST_BASE);

    wr32(MOVEP_TEST_BASE + 0x18, 0u);
    wr32(MOVEP_TEST_BASE + 0x1c, 0u);
    wr32(MOVEP_TEST_BASE + 0x30, 0u);
    wr32(MOVEP_TEST_BASE + 0x34, 0u);
    __asm__ volatile(
        "lea 0x01ff9c20,%%a0\n\t"
        "move.l #0x55667788,%%d0\n\t"
        "movep.l %%d0,-8(%%a0)\n\t"
        "moveq #0,%%d1\n\t"
        "movep.l -8(%%a0),%%d1\n\t"
        "move.l %%d1,0x01ff9c30\n\t"
        "move.l %%a0,0x01ff9c34"
        :
        :
        : "a0", "d0", "d1", "memory");
    chk32(0x000f0060u, rd32(MOVEP_TEST_BASE + 0x18), 0x55006600u);
    chk32(0x000f0064u, rd32(MOVEP_TEST_BASE + 0x1c), 0x77008800u);
    chk32(0x000f0068u, rd32(MOVEP_TEST_BASE + 0x30), 0x55667788u);
    chk32(0x000f006cu, rd32(MOVEP_TEST_BASE + 0x34), MOVEP_TEST_BASE + 0x20u);

    for (uint32_t off = 0x40u; off < 0x80u; off += 4u) {
        wr32(MOVEP_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "lea 0x01ff9c40,%%a0\n\t"
        "move.l #0x10203040,%%d0\n\t"
        "move.l #0x50607080,%%d1\n\t"
        "movep.l %%d0,0(%%a0)\n\t"
        "movep.l %%d1,8(%%a0)\n\t"
        "move.l %%a0,0x01ff9c70"
        :
        :
        : "a0", "d0", "d1", "memory");
    chk32(0x000f00b0u, rd32(MOVEP_TEST_BASE + 0x40), 0x10002000u);
    chk32(0x000f00b4u, rd32(MOVEP_TEST_BASE + 0x44), 0x30004000u);
    chk32(0x000f00b8u, rd32(MOVEP_TEST_BASE + 0x48), 0x50006000u);
    chk32(0x000f00bcu, rd32(MOVEP_TEST_BASE + 0x4c), 0x70008000u);
    chk32(0x000f00c0u, rd32(MOVEP_TEST_BASE + 0x70), MOVEP_TEST_BASE + 0x40u);

    wr32(MOVEP_TEST_BASE + 0x50, 0x9100a200u);
    wr32(MOVEP_TEST_BASE + 0x54, 0xb300c400u);
    wr32(MOVEP_TEST_BASE + 0x58, 0x11002200u);
    wr32(MOVEP_TEST_BASE + 0x5c, 0x33004400u);
    __asm__ volatile(
        "lea 0x01ff9c50,%%a0\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "movep.l 0(%%a0),%%d2\n\t"
        "movep.l 8(%%a0),%%d3\n\t"
        "move.l %%d2,0x01ff9c60\n\t"
        "move.l %%d3,0x01ff9c64\n\t"
        "move.l %%a0,0x01ff9c74"
        :
        :
        : "a0", "d2", "d3", "memory");
    chk32(0x000f00c4u, rd32(MOVEP_TEST_BASE + 0x60), 0x91a2b3c4u);
    chk32(0x000f00c8u, rd32(MOVEP_TEST_BASE + 0x64), 0x11223344u);
    chk32(0x000f00ccu, rd32(MOVEP_TEST_BASE + 0x74), MOVEP_TEST_BASE + 0x50u);

    for (uint32_t off = 0x80u; off < 0xc0u; off += 4u) {
        wr32(MOVEP_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "lea 0x01ff9c80,%%a0\n\t"
        "move.l #0x11223344,%%d0\n\t"
        "move.l #0xa1b2c3d4,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "movep.w %%d0,0(%%a0)\n\t"
        "movep.l %%d1,4(%%a0)\n\t"
        "movep.w 0(%%a0),%%d2\n\t"
        "movep.l 4(%%a0),%%d3\n\t"
        "move.l %%d2,0x01ff9cb0\n\t"
        "move.l %%d3,0x01ff9cb4\n\t"
        "move.l %%a0,0x01ff9cb8"
        :
        :
        : "a0", "d0", "d1", "d2", "d3", "memory");
    chk32(0x000f00d0u, rd32(MOVEP_TEST_BASE + 0x80), 0x33004400u);
    chk32(0x000f00d4u, rd32(MOVEP_TEST_BASE + 0x84), 0xa100b200u);
    chk32(0x000f00d8u, rd32(MOVEP_TEST_BASE + 0x88), 0xc300d400u);
    chk32(0x000f00dcu, rd32(MOVEP_TEST_BASE + 0xb0), 0x00003344u);
    chk32(0x000f00e0u, rd32(MOVEP_TEST_BASE + 0xb4), 0xa1b2c3d4u);
    chk32(0x000f00e4u, rd32(MOVEP_TEST_BASE + 0xb8), MOVEP_TEST_BASE + 0x80u);
}

void kmain(void)
{
    delay_poll_window();
    uart_puts("CORETEST START\n");

    test_aligned_long();
    test_byte_lanes_c();
    test_word_lanes_c();
    test_unaligned_lanes_asm();
    test_absolute_indexed_stores();
    test_full_format_indexed_memory_ops();
    test_indexed_ea_scale_directed();
    test_an_indexed_stores();
    test_an_post_pre_byte();
    test_movem_directed();
    test_control_flow_directed();
    test_stack_frame_control_directed();
    test_address_arithmetic_directed();
    test_register_transform_directed();
    test_unary_logic_directed();
    test_immediate_alu_directed();
    test_addx_subx_cmpm_memory_directed();
    test_system_control_directed();
    test_moves_directed();
    test_cmp2_chk2_directed();
    test_chk_directed();
    test_cas2_directed();
    test_return_control_directed();
    test_bcd_directed();
    test_pack_unpk_directed();
    test_exception_recovery_directed();
#ifdef CORETEST_SIM_IRQ
    test_interrupt_autovector_directed();
#endif
    test_alu_shift_bitfield_bcd_directed();
    test_condition_codes_directed();
    test_condition_consumers_directed();
    test_bitops_directed();
    test_signed_mul_div_directed();
    test_mul_div_memory_directed();
    test_memory_bitfield_directed();
    test_memory_bitfield_extended_directed();
    test_movep_tas_cas_directed();
    test_movep_displacement_directed();

    for (;;) {
        uart_puts("CORETEST PASS sum=");
        uart_hex32(g_sum);
        uart_putc('\n');
        delay_poll_window();
    }
}
