// Astra 68 pre-Harte CPU/SoC validation.
//
// Phase 1 focuses on memory destination effective-address writeback and
// byte-lane behavior, because absolute indexed byte stores already escaped.
#include <stdint.h>
#include "vesta.h"

#define SCRATCH_BASE 0x01ff9100u
#define SCRATCH      ((volatile uint8_t *)SCRATCH_BASE)

static volatile uint32_t g_sum;

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
}

static void test_control_flow_directed(void)
{
    uint32_t got;

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
}

static void test_system_control_directed(void)
{
    uint32_t got;

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

    wr32(SCRATCH_BASE + 0xf0, 0u);
    __asm__ volatile("trap #0" : : : "a0", "memory");
    chk32(0x000a0020u, rd32(SCRATCH_BASE + 0xf0), 1u);
    __asm__ volatile("trap #0" : : : "a0", "memory");
    chk32(0x000a0021u, rd32(SCRATCH_BASE + 0xf0), 2u);
}

static void test_alu_shift_bitfield_bcd_directed(void)
{
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

void kmain(void)
{
    delay_poll_window();
    uart_puts("CORETEST START\n");

    test_aligned_long();
    test_byte_lanes_c();
    test_word_lanes_c();
    test_absolute_indexed_stores();
    test_full_format_indexed_memory_ops();
    test_an_indexed_stores();
    test_an_post_pre_byte();
    test_movem_directed();
    test_control_flow_directed();
    test_system_control_directed();
    test_alu_shift_bitfield_bcd_directed();

    for (;;) {
        uart_puts("CORETEST PASS sum=");
        uart_hex32(g_sum);
        uart_putc('\n');
        delay_poll_window();
    }
}
