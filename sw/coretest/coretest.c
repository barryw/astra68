// Astra 68 pre-Harte CPU/SoC validation.
//
// Phase 1 focuses on memory destination effective-address writeback and
// byte-lane behavior, because absolute indexed byte stores already escaped.
#include <stdint.h>
#include "vega.h"
#include "vesta.h"

#define SCRATCH_BASE 0x01ff9100u
#define SCRATCH      ((volatile uint8_t *)SCRATCH_BASE)
#define EXC_REC_BASE (SCRATCH_BASE + 0x170u)
#define EXC_RECOVERY_PC (EXC_REC_BASE + 0x14u)
#define EXC_EXPECTED_ADDR (EXC_REC_BASE + 0x30u)
#define EXC_DATA_OUT (EXC_REC_BASE + 0x34u)
#define EXC_SKIP_DATA_CYCLE (EXC_REC_BASE + 0x38u)
#define EXC_STAGE_B_ADDR (EXC_REC_BASE + 0x3cu)
#define EXC_FAKE_STACK (SCRATCH_BASE + 0x300u)
#define EXC_ALT_VBR (SCRATCH_BASE + 0x400u)
#define IRQ_SIM_REQ 0xfff00600u
#define BERR_SIM_REQ 0xfff00604u
#define BERR_SIM_TARGET 0xfff00608u
#define FC_PROBE_ARM 0xfff0060cu
#define FC_PROBE_MOVES 1u
#define FC_PROBE_DATA_PROG 2u
#define BERR_ARM_SUP_DATA_READ "0x1b"
#define BERR_ARM_SUP_DATA_WRITE "0x0b"
#define BERR_ARM_SUP_PROG_READ "0x1d"
#define BERR_ARM_SUP_RMC_DATA_READ "0x3b"
#define BERR_ARM_USER_DATA_READ "0x13"
#define BERR_ARM_USER_DATA_WRITE "0x03"
#define BERR_ARM_USER_PROG_READ "0x15"
#define BERR_ARM_SDRAM_SUP_DATA_WRITE "0x4b"
#define BERR_SDRAM_TARGET 0x02000100u
#define BERR_UNMAPPED_TARGET 0xfff00900u
#ifdef CORETEST_SIM_FB_GUARD
#define BERR_SDRAM_ARM_ASM ""
#else
#define BERR_SDRAM_ARM_ASM \
    "move.l #" BERR_ARM_SDRAM_SUP_DATA_WRITE ",0xfff00604\n\t"
#endif
#define STACK_TEST_BASE (SCRATCH_BASE + 0x600u)
#define MOVES_TEST_BASE (SCRATCH_BASE + 0x700u)
#define MOVES_EXT_TEST_BASE (SCRATCH_BASE + 0x1b00u)
#define MOVES_FC_TEST_BASE (MOVES_EXT_TEST_BASE + 0x100u)
#define ATOMIC_RMC_TEST_BASE (MOVES_FC_TEST_BASE + 0x100u)
#define ATOMIC_RMC_EXPECT_CAS 1u
#define ATOMIC_RMC_EXPECT_TAS 2u
#define ATOMIC_RMC_EXPECT_CAS2 3u
#define ATOMIC_RMC_READ_ONLY 4u
#define DATA_FC_TEST_BASE (ATOMIC_RMC_TEST_BASE + 0x100u)
#define PROG_FC_TEST_BASE (DATA_FC_TEST_BASE + 0x20u)
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
#define ADDR_TEST_BASE (SCRATCH_BASE + 0x1300u)
#define UNARY_TEST_BASE (SCRATCH_BASE + 0x1400u)
#define IMM_TEST_BASE (SCRATCH_BASE + 0x1500u)
#define XMEM_TEST_BASE (SCRATCH_BASE + 0x1600u)
#define MUL_DIV_TEST_BASE (SCRATCH_BASE + 0x1800u)
#define CHK_TEST_BASE (SCRATCH_BASE + 0x1a00u)
#define DATA_ALU_TEST_BASE (SCRATCH_BASE + 0x1f00u)
#define RTE_USER_TEST_BASE (SCRATCH_BASE + 0x2100u)
#define IRQ_USER_TEST_BASE (SCRATCH_BASE + 0x2200u)

static volatile uint32_t g_sum;

extern void _h_default(void);
extern void _h_recover(void);
extern void _h_irq_return(void);
extern void _h_vesta_timer(void);
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
#ifdef CORETEST_SIM_IRQ
    const uint32_t limit = 32u;
#else
    const uint32_t limit = 10000u;
#endif
    for (volatile uint32_t delay = 0; delay < limit; ++delay) {}
}

static void progress_char(char c)
{
#ifdef CORETEST_SIM_PROGRESS
    uart_putc(c);
#else
    (void)c;
#endif
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
    wr32(EXC_REC_BASE + 0x24, 0u);
    wr32(EXC_REC_BASE + 0x28, 0u);
    wr32(EXC_REC_BASE + 0x2c, 0u);
    wr32(EXC_EXPECTED_ADDR, 0u);
    wr32(EXC_DATA_OUT, 0u);
    wr32(EXC_SKIP_DATA_CYCLE, 0u);
    wr32(EXC_STAGE_B_ADDR, 0u);
}

static void arm_exception_recovery_skip_data_cycle(uint32_t vector_offset)
{
    arm_exception_recovery(vector_offset);
    wr32(EXC_SKIP_DATA_CYCLE, 1u);
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

static void chk_exception_pc(uint32_t id, uint32_t pc_exp)
{
    chk32(id, rd32(EXC_REC_BASE + 0x08), pc_exp);
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

static void chk_access_fault_status(uint32_t id, uint32_t status_exp)
{
    uint32_t status = rd32(EXC_REC_BASE + 0x24) & 0xffffu;

    chk32(id, status, status_exp);
}

static void chk_access_fault_long(uint32_t id, uint32_t frame_offset,
                                  uint32_t exp)
{
    chk32(id, rd32(EXC_REC_BASE + frame_offset), exp);
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

    wr32(SCRATCH_BASE + 0x78, 0x00000000u);
    __asm__ volatile(
        "move.l #1,%%d4\n\t"
        "move.b #0x96,0x01ff9178(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk32(0x00040079u, rd32(SCRATCH_BASE + 0x78), 0x00960000u);

    wr32(SCRATCH_BASE + 0x100, 0x11223344u);
    wr32(SCRATCH_BASE + 0x104, 0x55667788u);
    wr32(SCRATCH_BASE + 0x108, 0x99aabbccu);
    __asm__ volatile(
        "moveq #0,%%d4\n\t"
        "move.b #0xa0,0x01ff9204(%%d4:l)\n\t"
        "moveq #1,%%d4\n\t"
        "move.b #0xa1,0x01ff9204(%%d4:l)\n\t"
        "moveq #2,%%d4\n\t"
        "move.b #0xa2,0x01ff9204(%%d4:l)\n\t"
        "moveq #3,%%d4\n\t"
        "move.b #0xa3,0x01ff9204(%%d4:l)"
        :
        :
        : "d4", "memory");
    chk32(0x00040080u, rd32(SCRATCH_BASE + 0x100), 0x11223344u);
    chk32(0x00040084u, rd32(SCRATCH_BASE + 0x104), 0xa0a1a2a3u);
    chk32(0x00040088u, rd32(SCRATCH_BASE + 0x108), 0x99aabbccu);

    wr32(SCRATCH_BASE + 0x10c, 0x10203040u);
    wr32(SCRATCH_BASE + 0x110, 0x50607080u);
    wr32(SCRATCH_BASE + 0x114, 0x90a0b0c0u);
    __asm__ volatile(
        "move.l #0x7fff0000,%%d4\n\t"
        "move.b #0xb0,0x01ff9210(%%d4:w)\n\t"
        "move.l #0x7fff0001,%%d4\n\t"
        "move.b #0xb1,0x01ff9210(%%d4:w)\n\t"
        "move.l #0x7fff0002,%%d4\n\t"
        "move.b #0xb2,0x01ff9210(%%d4:w)\n\t"
        "move.l #0x7fff0003,%%d4\n\t"
        "move.b #0xb3,0x01ff9210(%%d4:w)"
        :
        :
        : "d4", "memory");
    chk32(0x0004008cu, rd32(SCRATCH_BASE + 0x10c), 0x10203040u);
    chk32(0x00040090u, rd32(SCRATCH_BASE + 0x110), 0xb0b1b2b3u);
    chk32(0x00040094u, rd32(SCRATCH_BASE + 0x114), 0x90a0b0c0u);

    wr32(SCRATCH_BASE + 0x120, 0x01020304u);
    wr32(SCRATCH_BASE + 0x128, 0x00000000u);
    __asm__ volatile(
        "move.l #0x7fff0002,%%d4\n\t"
        "move.b #0xb4,0x01ff9220(%%d4:w:4)"
        :
        :
        : "d4", "memory");
    chk32(0x00040098u, rd32(SCRATCH_BASE + 0x120), 0x01020304u);
    chk32(0x0004009cu, rd32(SCRATCH_BASE + 0x128), 0xb4000000u);

    wr32(SCRATCH_BASE + 0x130, 0x00000000u);
    wr32(SCRATCH_BASE + 0x134, 0x00000000u);
    wr32(SCRATCH_BASE + 0x138, 0x00000000u);
    __asm__ volatile(
        "move.l #0x0000ffff,%%d4\n\t"
        "move.b #0xb5,0x01ff923c(%%d4:w:8)"
        :
        :
        : "d4", "memory");
    chk32(0x000400a0u, rd32(SCRATCH_BASE + 0x130), 0u);
    chk32(0x000400a4u, rd32(SCRATCH_BASE + 0x134), 0xb5000000u);
    chk32(0x000400a8u, rd32(SCRATCH_BASE + 0x138), 0u);
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

    wr32(FULLFMT_TEST_BASE + 0x120u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "moveq #0,%%d4\n\t"
        "move.b #0xc0,0x0120(%%a0,%%d4:l)\n\t"
        "moveq #1,%%d4\n\t"
        "move.b #0xc1,0x0120(%%a0,%%d4:l)\n\t"
        "moveq #2,%%d4\n\t"
        "move.b #0xc2,0x0120(%%a0,%%d4:l)\n\t"
        "moveq #3,%%d4\n\t"
        "move.b #0xc3,0x0120(%%a0,%%d4:l)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701e0u, rd32(FULLFMT_TEST_BASE + 0x120u), 0xc0c1c2c3u);

    wr32(FULLFMT_TEST_BASE + 0x48u, FULLFMT_TEST_BASE + 0x150u);
    wr32(FULLFMT_TEST_BASE + 0x150u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "moveq #4,%%d4\n\t"
        "move.b #0xd0,([0x40,%%a0,%%d4:l:2],0x00)\n\t"
        "move.b #0xd1,([0x40,%%a0,%%d4:l:2],0x01)\n\t"
        "move.b #0xd2,([0x40,%%a0,%%d4:l:2],0x02)\n\t"
        "move.b #0xd3,([0x40,%%a0,%%d4:l:2],0x03)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701e4u, rd32(FULLFMT_TEST_BASE + 0x150u), 0xd0d1d2d3u);

    wr32(FULLFMT_TEST_BASE + 0x50u, FULLFMT_TEST_BASE + 0x160u);
    wr32(FULLFMT_TEST_BASE + 0x160u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "moveq #0,%%d4\n\t"
        "move.b #0xe0,([0x50,%%a0],%%d4:l,0x00)\n\t"
        "moveq #1,%%d4\n\t"
        "move.b #0xe1,([0x50,%%a0],%%d4:l,0x00)\n\t"
        "moveq #2,%%d4\n\t"
        "move.b #0xe2,([0x50,%%a0],%%d4:l,0x00)\n\t"
        "moveq #3,%%d4\n\t"
        "move.b #0xe3,([0x50,%%a0],%%d4:l,0x00)"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701e8u, rd32(FULLFMT_TEST_BASE + 0x160u), 0xe0e1e2e3u);

    wr32(FULLFMT_TEST_BASE + 0x1e0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x1ecu, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #7,%%d4\n\t"
        ".word 0x11bc,0x00e4,0x4b60,0x01e0"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701ecu, rd32(FULLFMT_TEST_BASE + 0x1e0u), 0xe4000000u);
    chk32(0x000701f0u, rd32(FULLFMT_TEST_BASE + 0x1ecu), 0u);

    wr32(FULLFMT_TEST_BASE + 0x1f0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x200u, 0u);
    __asm__ volatile(
        "movea.l #0x10,%%a0\n\t"
        "move.l #5,%%d4\n\t"
        ".word 0x11bc,0x00e5,0x4bb0,0x01ff,0xa0e6"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701f4u, rd32(FULLFMT_TEST_BASE + 0x1f0u), 0xe5000000u);
    chk32(0x000701f8u, rd32(FULLFMT_TEST_BASE + 0x200u), 0u);

    wr32(FULLFMT_TEST_BASE + 0x70u, FULLFMT_TEST_BASE + 0x280u);
    wr32(FULLFMT_TEST_BASE + 0x284u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x28cu, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #5,%%d4\n\t"
        ".word 0x11bc,0x00e6,0x4b66,0x0070,0x0004"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x000701fcu, rd32(FULLFMT_TEST_BASE + 0x284u), 0xe6000000u);
    chk32(0x000701fdu, rd32(FULLFMT_TEST_BASE + 0x28cu), 0u);

    wr32(FULLFMT_TEST_BASE + 0x2a0u, FULLFMT_TEST_BASE + 0x2d0u);
    wr32(FULLFMT_TEST_BASE + 0x2b0u, FULLFMT_TEST_BASE + 0x2e0u);
    wr32(FULLFMT_TEST_BASE + 0x2d0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x2e0u, 0u);
    __asm__ volatile(
        "movea.l #0x10,%%a0\n\t"
        "move.l #4,%%d4\n\t"
        ".word 0x11bc,0x00e7,0x4bb2,0x01ff,0xa198,0x0003"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x0007027cu, rd32(FULLFMT_TEST_BASE + 0x2d0u), 0x000000e7u);
    chk32(0x00070280u, rd32(FULLFMT_TEST_BASE + 0x2e0u), 0u);

    wr32(FULLFMT_TEST_BASE + 0x88u, FULLFMT_TEST_BASE + 0x300u);
    wr32(FULLFMT_TEST_BASE + 0x90u, FULLFMT_TEST_BASE + 0x320u);
    wr32(FULLFMT_TEST_BASE + 0x300u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x304u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x308u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x30cu, 0u);
    wr32(FULLFMT_TEST_BASE + 0x32cu, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "move.l #4,%%d4\n\t"
        ".word 0x11bc,0x00ea,0x4b27,0x0088,0x0000,0x0005"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00070284u, rd32(FULLFMT_TEST_BASE + 0x300u), 0u);
    chk32(0x00070288u, rd32(FULLFMT_TEST_BASE + 0x304u), 0u);
    chk32(0x0007028cu, rd32(FULLFMT_TEST_BASE + 0x308u), 0u);
    chk32(0x00070290u, rd32(FULLFMT_TEST_BASE + 0x30cu), 0x00ea0000u);
    chk32(0x00070294u, rd32(FULLFMT_TEST_BASE + 0x32cu), 0u);

    wr32(FULLFMT_TEST_BASE + 0x340u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x348u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x370u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x378u, 0u);
    __asm__ volatile(
        "lea 0x01ffa248,%%a0\n\t"
        "moveq #0,%%d4\n\t"
        ".word 0x11bc,0x00eb,0x4b20,0xfff8"
        :
        :
        : "a0", "d4", "memory");
    chk32(0x00070298u, rd32(FULLFMT_TEST_BASE + 0x340u), 0xeb000000u);
    chk32(0x0007029cu, rd32(FULLFMT_TEST_BASE + 0x348u), 0u);
    chk32(0x000702a0u, rd32(FULLFMT_TEST_BASE + 0x370u), 0u);
    chk32(0x000702a4u, rd32(FULLFMT_TEST_BASE + 0x378u), 0u);

    wr32(FULLFMT_TEST_BASE + 0x390u, FULLFMT_TEST_BASE + 0x3a0u);
    wr32(FULLFMT_TEST_BASE + 0x3a0u, 0x11223344u);
    wr32(FULLFMT_TEST_BASE + 0x394u, FULLFMT_TEST_BASE + 0x3b0u);
    wr32(FULLFMT_TEST_BASE + 0x3bcu, 0u);
    wr32(FULLFMT_TEST_BASE + 0x398u, FULLFMT_TEST_BASE + 0x3c0u);
    wr32(FULLFMT_TEST_BASE + 0x3c4u, 0x00007fffu);
    wr32(FULLFMT_TEST_BASE + 0x3a4u, FULLFMT_TEST_BASE + 0x3d8u);
    wr32(FULLFMT_TEST_BASE + 0x3d8u, 0x0f0f00f0u);
    wr32(FULLFMT_TEST_BASE + 0x3e0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x3e4u, 0u);
    __asm__ volatile(
        "lea 0x01ff9f00,%%a0\n\t"
        "moveq #8,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "move.w ([0x380,%%a0,%%d4:l:2],0x02),%%d0\n\t"
        "moveq #3,%%d4\n\t"
        "move.l #0x55667788,%%d1\n\t"
        "move.l %%d1,([0x394,%%a0],%%d4:l:4,0x00)\n\t"
        "moveq #2,%%d4\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "add.w %%d1,([0x398,%%a0],%%d4:l:2,0x02)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ffa2e0\n\t"
        "moveq #4,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "not.l ([0x39c,%%a0,%%d4:l:2])\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ffa2e4\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "a0", "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000702b0u, got, 0x3344u);
    chk32(0x000702b4u, rd32(FULLFMT_TEST_BASE + 0x3bcu), 0x55667788u);
    chk32(0x000702b8u, rd32(FULLFMT_TEST_BASE + 0x3c4u), 0x00008000u);
    chk32(0x000702bcu, rd32(FULLFMT_TEST_BASE + 0x3d8u), 0xf0f0ff0fu);
    chk32(0x000702c0u, rd32(FULLFMT_TEST_BASE + 0x3e0u) & 0x1fu, 0x0au);
    chk32(0x000702c4u, rd32(FULLFMT_TEST_BASE + 0x3e4u) & 0x1fu, 0x18u);

    wr32(FULLFMT_TEST_BASE + 0x3e8u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x3f0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x3f4u, FULLFMT_TEST_BASE + 0x3f8u);
    wr32(FULLFMT_TEST_BASE + 0x3f8u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x3fcu, 0xee112233u);
    __asm__ volatile(
        "moveq #7,%%d4\n\t"
        ".word 0x11bc,0x00ec,0x4bf0\n\t"
        ".long 0x01ffa2f0\n\t"
        ".word 0x11bc,0x00ed,0x4bf2\n\t"
        ".long 0x01ffa2f4\n\t"
        ".word 0x0003\n\t"
        "moveq #0,%%d0\n\t"
        ".word 0x103b,0x4bf0\n\t"
        ".long 0x01ffa2fc\n\t"
        "move.l %%d0,0x01ffa2e8"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x000702c8u, rd32(FULLFMT_TEST_BASE + 0x3f0u), 0xec000000u);
    chk32(0x000702ccu, rd32(FULLFMT_TEST_BASE + 0x3f4u), FULLFMT_TEST_BASE + 0x3f8u);
    chk32(0x000702d0u, rd32(FULLFMT_TEST_BASE + 0x3f8u), 0x000000edu);
    chk32(0x000702d4u, rd32(FULLFMT_TEST_BASE + 0x3e8u), 0xeeu);
    chk32(0x000702d8u, rd32(FULLFMT_TEST_BASE + 0x3fcu), 0xee112233u);
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

static void test_pc_indexed_data_directed(void)
{
    wr32(FULLFMT_TEST_BASE + 0x170u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x174u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x178u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x17cu, 0u);

    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d4\n\t"
        "move.b 1f(%%pc,%%d4:l),%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "moveq #2,%%d4\n\t"
        "move.w 1f(%%pc,%%d4:l),%%d1\n\t"
        "moveq #4,%%d4\n\t"
        "move.l 1f(%%pc,%%d4:l),%%d2\n\t"
        "moveq #-4,%%d4\n\t"
        "move.l 2f(%%pc,%%d4:w),%%d3\n\t"
        "move.l %%d0,0x01ffa070\n\t"
        "move.l %%d1,0x01ffa074\n\t"
        "move.l %%d2,0x01ffa078\n\t"
        "move.l %%d3,0x01ffa07c\n\t"
        "bra 3f\n"
        ".balign 2\n"
        "1:\n\t"
        ".byte 0x5a,0xa5\n\t"
        ".word 0x2468\n\t"
        ".long 0x13579bdf\n"
        "2:\n\t"
        "3:"
        :
        :
        : "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x00070250u, rd32(FULLFMT_TEST_BASE + 0x170u), 0xa5u);
    chk32(0x00070254u, rd32(FULLFMT_TEST_BASE + 0x174u), 0x2468u);
    chk32(0x00070258u, rd32(FULLFMT_TEST_BASE + 0x178u), 0x13579bdfu);
    chk32(0x0007025cu, rd32(FULLFMT_TEST_BASE + 0x17cu), 0x13579bdfu);

    wr32(FULLFMT_TEST_BASE + 0x190u, 0x5aa55aa5u);
    wr32(FULLFMT_TEST_BASE + 0x194u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x1a8u, 0x1122cc44u);
    wr32(FULLFMT_TEST_BASE + 0x1b0u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x1c0u, 0x55667788u);
    wr32(FULLFMT_TEST_BASE + 0x1d0u, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l ([1f,%%pc],0x00),%%d0\n\t"
        "move.l %%d0,0x01ffa094\n\t"
        "moveq #2,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "move.b ([2f,%%pc],%%d4:l:4,0x02),%%d0\n\t"
        "move.l %%d0,0x01ffa0b0\n\t"
        "moveq #2,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "move.b ([3f-8,%%pc,%%d4:l:4],0x01),%%d0\n\t"
        "move.l %%d0,0x01ffa0d0\n\t"
        "bra 4f\n"
        ".balign 2\n"
        "1:\n\t"
        ".long 0x01ffa090\n"
        "2:\n\t"
        ".long 0x01ffa0a0\n"
        "3:\n\t"
        ".long 0x01ffa0c0\n"
        "4:"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x00070260u, rd32(FULLFMT_TEST_BASE + 0x194u), 0x5aa55aa5u);
    chk32(0x00070264u, rd32(FULLFMT_TEST_BASE + 0x1b0u), 0xccu);
    chk32(0x00070268u, rd32(FULLFMT_TEST_BASE + 0x1d0u), 0x66u);

    wr32(FULLFMT_TEST_BASE + 0x210u, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l #1,%%d4\n\t"
        ".word 0x103b,0x4b60\n\t"
        ".word 1f-(.-2)\n\t"
        "move.l %%d0,0x01ffa110\n\t"
        "bra 2f\n"
        ".balign 2\n"
        "1:\n\t"
        ".byte 0xa6,0x00,0x5b,0x00\n"
        "2:"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x0007026cu, rd32(FULLFMT_TEST_BASE + 0x210u), 0xa6u);

    wr32(FULLFMT_TEST_BASE + 0x220u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x230u, 0xa7000000u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l #5,%%d4\n\t"
        ".word 0x103b,0x4bb0\n\t"
        ".long 0x01ffa126\n\t"
        "move.l %%d0,0x01ffa120"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x00070270u, rd32(FULLFMT_TEST_BASE + 0x220u), 0xa7u);

    wr32(FULLFMT_TEST_BASE + 0x240u, FULLFMT_TEST_BASE + 0x250u);
    wr32(FULLFMT_TEST_BASE + 0x250u, 0x112233a8u);
    wr32(FULLFMT_TEST_BASE + 0x260u, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l #2,%%d4\n\t"
        ".word 0x103b,0x4db2\n\t"
        ".long 0x01ffa138\n\t"
        ".word 0x0003\n\t"
        "move.l %%d0,0x01ffa160"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x00070274u, rd32(FULLFMT_TEST_BASE + 0x260u), 0xa8u);

    wr32(FULLFMT_TEST_BASE + 0x270u, 0u);
    wr32(FULLFMT_TEST_BASE + 0x284u, 0xa9000000u);
    wr32(FULLFMT_TEST_BASE + 0x28cu, 0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "move.l #5,%%d4\n\t"
        ".word 0x103b,0x4b66\n\t"
        ".word 1f-(.-2)\n\t"
        ".word 0x0004\n\t"
        "move.l %%d0,0x01ffa170\n\t"
        "bra 2f\n"
        ".balign 2\n"
        "1:\n\t"
        ".long 0x01ffa180\n"
        "2:"
        :
        :
        : "d0", "d4", "cc", "memory");
    chk32(0x00070278u, rd32(FULLFMT_TEST_BASE + 0x270u), 0xa9u);
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

static void test_memory_to_memory_move_directed(void)
{
    const uint32_t base = XMEM_TEST_BASE + 0x200u;
    uint32_t got0;
    uint32_t got1;

    wr32(base + 0x00u, 0x005a0000u);
    wr32(base + 0x80u, 0u);
    __asm__ volatile(
        "lea 0x01ffa901,%%a0\n\t"
        "lea 0x01ffa981,%%a1\n\t"
        "move.b (%%a0),(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097000u, got0, base + 0x01u);
    chk32(0x00097004u, got1, base + 0x81u);
    chk32(0x00097008u, rd32(base + 0x80u), 0x005a0000u);

    wr32(base + 0x10u, 0x12340000u);
    wr32(base + 0x90u, 0u);
    __asm__ volatile(
        "lea 0x01ffa910,%%a0\n\t"
        "lea 0x01ffa990,%%a1\n\t"
        "move.w (%%a0),(%%a1)+\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097010u, got0, base + 0x10u);
    chk32(0x00097014u, got1, base + 0x92u);
    chk32(0x00097018u, rd32(base + 0x90u), 0x12340000u);

    wr32(base + 0x20u, 0x89abcdefu);
    wr32(base + 0xa0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa920,%%a0\n\t"
        "lea 0x01ffa9a4,%%a1\n\t"
        "move.l (%%a0),-(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097020u, got0, base + 0x20u);
    chk32(0x00097024u, got1, base + 0xa0u);
    chk32(0x00097028u, rd32(base + 0xa0u), 0x89abcdefu);

    wr32(base + 0x30u, 0x00660000u);
    wr32(base + 0xb0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa931,%%a0\n\t"
        "lea 0x01ffa9b1,%%a1\n\t"
        "move.b (%%a0)+,(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097030u, got0, base + 0x32u);
    chk32(0x00097034u, got1, base + 0xb1u);
    chk32(0x00097038u, rd32(base + 0xb0u), 0x00660000u);

    wr32(base + 0x40u, 0x56780000u);
    wr32(base + 0xc0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa940,%%a0\n\t"
        "lea 0x01ffa9c0,%%a1\n\t"
        "move.w (%%a0)+,(%%a1)+\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097040u, got0, base + 0x42u);
    chk32(0x00097044u, got1, base + 0xc2u);
    chk32(0x00097048u, rd32(base + 0xc0u), 0x56780000u);

    wr32(base + 0x50u, 0x0badc0deu);
    wr32(base + 0xd0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa950,%%a0\n\t"
        "lea 0x01ffa9d4,%%a1\n\t"
        "move.l (%%a0)+,-(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097050u, got0, base + 0x54u);
    chk32(0x00097054u, got1, base + 0xd0u);
    chk32(0x00097058u, rd32(base + 0xd0u), 0x0badc0deu);

    wr32(base + 0x60u, 0x7c000000u);
    wr32(base + 0xe0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa961,%%a0\n\t"
        "lea 0x01ffa9e3,%%a1\n\t"
        "move.b -(%%a0),(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097060u, got0, base + 0x60u);
    chk32(0x00097064u, got1, base + 0xe3u);
    chk32(0x00097068u, rd32(base + 0xe0u), 0x0000007cu);

    wr32(base + 0x70u, 0x9abc0000u);
    wr32(base + 0xf0u, 0u);
    __asm__ volatile(
        "lea 0x01ffa972,%%a0\n\t"
        "lea 0x01ffa9f0,%%a1\n\t"
        "move.w -(%%a0),(%%a1)+\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097070u, got0, base + 0x70u);
    chk32(0x00097074u, got1, base + 0xf2u);
    chk32(0x00097078u, rd32(base + 0xf0u), 0x9abc0000u);

    wr32(base + 0x80u, 0x13579bdfu);
    wr32(base + 0x100u, 0u);
    __asm__ volatile(
        "lea 0x01ffa984,%%a0\n\t"
        "lea 0x01ffaa04,%%a1\n\t"
        "move.l -(%%a0),-(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x00097080u, got0, base + 0x80u);
    chk32(0x00097084u, got1, base + 0x100u);
    chk32(0x00097088u, rd32(base + 0x100u), 0x13579bdfu);

    wr32(base + 0x120u, 0x1122aa44u);
    wr32(base + 0x1a0u, 0u);
    __asm__ volatile(
        "lea 0x01ffaa10,%%a0\n\t"
        "lea 0x01ffaa90,%%a1\n\t"
        "move.l #0x12,%%d4\n\t"
        "move.l #0x13,%%d5\n\t"
        "move.b 0(%%a0,%%d4:l),0(%%a1,%%d5:l)"
        :
        :
        : "a0", "a1", "d4", "d5", "cc", "memory");
    chk32(0x00097090u, rd32(base + 0x1a0u), 0x000000aau);

    wr32(base + 0x130u, 0x55aa66bbu);
    wr32(base + 0x1b4u, 0u);
    __asm__ volatile(
        "lea 0x01ffaa20,%%a0\n\t"
        "lea 0x01ffaaa0,%%a1\n\t"
        "moveq #3,%%d4\n\t"
        "movea.l #9,%%a2\n\t"
        "move.w 6(%%a0,%%d4:w:4),2(%%a1,%%a2:l:2)"
        :
        :
        : "a0", "a1", "a2", "d4", "cc", "memory");
    chk32(0x00097094u, rd32(base + 0x1b4u), 0x66bb0000u);

    wr32(base + 0x140u, 0xcafebabeu);
    wr32(base + 0x1c4u, 0u);
    __asm__ volatile(
        "lea 0x01ffaa44,%%a0\n\t"
        "lea 0x01ffaab0,%%a1\n\t"
        "moveq #0,%%d4\n\t"
        "moveq #8,%%d5\n\t"
        "move.l -4(%%a0,%%d4:w),4(%%a1,%%d5:l:2)"
        :
        :
        : "a0", "a1", "d4", "d5", "cc", "memory");
    chk32(0x00097098u, rd32(base + 0x1c4u), 0xcafebabeu);
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

    wr32(SCRATCH_BASE + 0x2d0, 0x01020304u);
    wr32(SCRATCH_BASE + 0x2d4, 0x11223344u);
    wr32(SCRATCH_BASE + 0x2d8, 0x55667788u);
    wr32(SCRATCH_BASE + 0x2e0, 0u);
    wr32(SCRATCH_BASE + 0x2e4, 0u);
    wr32(SCRATCH_BASE + 0x2e8, 0u);
    __asm__ volatile(
        "lea 0x01ff93d0,%%a4\n\t"
        "movem.l (%%a4),%%d0/%%a4-%%a5\n\t"
        "move.l %%d0,0x01ff93e0\n\t"
        "move.l %%a4,0x01ff93e4\n\t"
        "move.l %%a5,0x01ff93e8"
        :
        :
        : "d0", "a4", "a5", "memory");
    chk32(0x000801f0u, rd32(SCRATCH_BASE + 0x2e0), 0x01020304u);
    chk32(0x000801f4u, rd32(SCRATCH_BASE + 0x2e4), 0x11223344u);
    chk32(0x000801f8u, rd32(SCRATCH_BASE + 0x2e8), 0x55667788u);

    wr32(SCRATCH_BASE + 0x2f8, 0u);
    wr32(SCRATCH_BASE + 0x2fc, 0u);
    __asm__ volatile(
        "lea 0x01ff9400,%%a2\n\t"
        "move.l #0x11223344,%%d0\n\t"
        "movem.l %%d0/%%a2,-(%%a2)\n\t"
        "move.l %%a2,0x01ff9400"
        :
        :
        : "d0", "a2", "memory");
    chk32(0x00080200u, rd32(SCRATCH_BASE + 0x2f8), 0x11223344u);
    chk32(0x00080204u, rd32(SCRATCH_BASE + 0x2fc), SCRATCH_BASE + 0x2fcu);
    chk32(0x00080208u, rd32(SCRATCH_BASE + 0x300), SCRATCH_BASE + 0x2f8u);

    wr32(SCRATCH_BASE + 0x2f8, 0u);
    wr32(SCRATCH_BASE + 0x2fc, 0u);
    wr32(SCRATCH_BASE + 0x300, 0u);
    __asm__ volatile(
        "lea 0x01ff9400,%%a2\n\t"
        "move.l #0xaaaabeef,%%d0\n\t"
        "movem.w %%d0/%%a2,-(%%a2)\n\t"
        "move.l %%a2,0x01ff9400"
        :
        :
        : "d0", "a2", "memory");
    chk32(0x00080210u, rd32(SCRATCH_BASE + 0x2f8), 0u);
    chk32(0x00080214u, rd32(SCRATCH_BASE + 0x2fc), 0xbeef93feu);
    chk32(0x00080218u, rd32(SCRATCH_BASE + 0x300), SCRATCH_BASE + 0x2fcu);

    wr32(SCRATCH_BASE + 0x2f0, 0x12345678u);
    wr32(SCRATCH_BASE + 0x2f4, 0xdeadbeefu);
    wr32(SCRATCH_BASE + 0x2f8, 0u);
    wr32(SCRATCH_BASE + 0x2fc, 0u);
    __asm__ volatile(
        "lea 0x01ff93f0,%%a2\n\t"
        "movem.l (%%a2)+,%%d0/%%a2\n\t"
        "move.l %%d0,0x01ff93f8\n\t"
        "move.l %%a2,0x01ff93fc"
        :
        :
        : "d0", "a2", "memory");
    chk32(0x00080220u, rd32(SCRATCH_BASE + 0x2f8), 0x12345678u);
    chk32(0x00080224u, rd32(SCRATCH_BASE + 0x2fc), SCRATCH_BASE + 0x2f8u);

    wr32(SCRATCH_BASE + 0x2f0, 0x80007fffu);
    wr32(SCRATCH_BASE + 0x2f4, 0u);
    wr32(SCRATCH_BASE + 0x2f8, 0u);
    wr32(SCRATCH_BASE + 0x2fc, 0u);
    __asm__ volatile(
        "lea 0x01ff93f0,%%a2\n\t"
        "movem.w (%%a2)+,%%d0/%%a2\n\t"
        "move.l %%d0,0x01ff93f8\n\t"
        "move.l %%a2,0x01ff93fc"
        :
        :
        : "d0", "a2", "memory");
    chk32(0x00080230u, rd32(SCRATCH_BASE + 0x2f8), 0xffff8000u);
    chk32(0x00080234u, rd32(SCRATCH_BASE + 0x2fc), SCRATCH_BASE + 0x2f4u);

    for (uint32_t off = 0x320u; off < 0x390u; off += 4u) {
        wr32(SCRATCH_BASE + off, 0u);
    }
    __asm__ volatile(
        "lea 0x01ff9410,%%a2\n\t"
        "moveq #8,%%d4\n\t"
        "move.l #0x10203040,%%d0\n\t"
        "move.l #0x50607080,%%d1\n\t"
        "move.l #0x90a0b0c0,%%d2\n\t"
        "move.l #0xd0e0f001,%%d3\n\t"
        "movem.l %%d0-%%d3,0(%%a2,%%d4:l:2)"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "d4", "memory");
    chk32(0x00080240u, rd32(SCRATCH_BASE + 0x320), 0x10203040u);
    chk32(0x00080244u, rd32(SCRATCH_BASE + 0x324), 0x50607080u);
    chk32(0x00080248u, rd32(SCRATCH_BASE + 0x328), 0x90a0b0c0u);
    chk32(0x0008024cu, rd32(SCRATCH_BASE + 0x32c), 0xd0e0f001u);

    __asm__ volatile(
        "lea 0x01ff9410,%%a2\n\t"
        "moveq #8,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "movem.l 0(%%a2,%%d4:l:2),%%d0-%%d3\n\t"
        "move.l %%d0,0x01ff9440\n\t"
        "move.l %%d1,0x01ff9444\n\t"
        "move.l %%d2,0x01ff9448\n\t"
        "move.l %%d3,0x01ff944c"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "d4", "memory");
    chk32(0x00080250u, rd32(SCRATCH_BASE + 0x340), 0x10203040u);
    chk32(0x00080254u, rd32(SCRATCH_BASE + 0x344), 0x50607080u);
    chk32(0x00080258u, rd32(SCRATCH_BASE + 0x348), 0x90a0b0c0u);
    chk32(0x0008025cu, rd32(SCRATCH_BASE + 0x34c), 0xd0e0f001u);

    wr32(SCRATCH_BASE + 0x360, 0x80007fffu);
    wr32(SCRATCH_BASE + 0x364, 0x0001ffffu);
    __asm__ volatile(
        "lea 0x01ff9450,%%a2\n\t"
        "moveq #8,%%d4\n\t"
        "movem.w 0(%%a2,%%d4:l:2),%%d0-%%d3\n\t"
        "move.l %%d0,0x01ff9470\n\t"
        "move.l %%d1,0x01ff9474\n\t"
        "move.l %%d2,0x01ff9478\n\t"
        "move.l %%d3,0x01ff947c"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "d4", "memory");
    chk32(0x00080260u, rd32(SCRATCH_BASE + 0x370), 0xffff8000u);
    chk32(0x00080264u, rd32(SCRATCH_BASE + 0x374), 0x00007fffu);
    chk32(0x00080268u, rd32(SCRATCH_BASE + 0x378), 0x00000001u);
    chk32(0x0008026cu, rd32(SCRATCH_BASE + 0x37c), 0xffffffffu);

    __asm__ volatile(
        "lea 0x01ff9470,%%a2\n\t"
        "moveq #8,%%d4\n\t"
        "move.l #0xaaaa1111,%%d0\n\t"
        "move.l #0xbbbb2222,%%d1\n\t"
        "move.l #0xcccc3333,%%d2\n\t"
        "move.l #0xdddd4444,%%d3\n\t"
        "movem.w %%d0-%%d3,0(%%a2,%%d4:l:2)"
        :
        :
        : "a2", "d0", "d1", "d2", "d3", "d4", "memory");
    chk32(0x00080270u, rd32(SCRATCH_BASE + 0x380), 0x11112222u);
    chk32(0x00080274u, rd32(SCRATCH_BASE + 0x384), 0x33334444u);
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

    wr32(STACK_TEST_BASE + 0x00u, 0u);
    wr32(STACK_TEST_BASE + 0x04u, 0x11223344u);
    wr32(STACK_TEST_BASE + 0x08u, 0xaabbccddu);
    wr32(STACK_TEST_BASE + 0xf0u, 0u);
    wr32(STACK_TEST_BASE + 0xf4u, 0u);
    wr32(STACK_TEST_BASE + 0xf8u, 0u);
    wr32(STACK_TEST_BASE + 0xfcu, 0u);
    wr32(STACK_TEST_BASE + 0xe8u, 0u);
    wr32(STACK_TEST_BASE + 0xecu, 0u);
    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ff9700,%%sp\n\t"
        "move.b #0x83,(%%sp)+\n\t"
        "move.l %%sp,0x01ff97f0\n\t"
        "lea 0x01ff9704,%%sp\n\t"
        "move.b #0x84,-(%%sp)\n\t"
        "move.l %%sp,0x01ff97f4\n\t"
        "lea 0x01ff9704,%%sp\n\t"
        "moveq #0,%%d0\n\t"
        "move.b (%%sp)+,%%d0\n\t"
        "move.l %%d0,0x01ff97f8\n\t"
        "move.l %%sp,0x01ff97fc\n\t"
        "lea 0x01ff970a,%%sp\n\t"
        "moveq #0,%%d1\n\t"
        "move.b -(%%sp),%%d1\n\t"
        "move.l %%d1,0x01ff97e8\n\t"
        "move.l %%sp,0x01ff97ec\n\t"
        "move.l %%a2,%%sp"
        :
        :
        : "a2", "d0", "d1", "cc", "memory");
    chk32(0x000910c0u, rd32(STACK_TEST_BASE + 0x00u), 0x83008400u);
    chk32(0x000910c4u, rd32(STACK_TEST_BASE + 0xf0u), STACK_TEST_BASE + 0x02u);
    chk32(0x000910c8u, rd32(STACK_TEST_BASE + 0xf4u), STACK_TEST_BASE + 0x02u);
    chk32(0x000910ccu, rd32(STACK_TEST_BASE + 0xf8u), 0x11u);
    chk32(0x000910d0u, rd32(STACK_TEST_BASE + 0xfcu), STACK_TEST_BASE + 0x06u);
    chk32(0x000910d4u, rd32(STACK_TEST_BASE + 0xe8u), 0xaau);
    chk32(0x000910d8u, rd32(STACK_TEST_BASE + 0xecu), STACK_TEST_BASE + 0x08u);

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

    for (uint32_t off = 0x180u; off <= 0x19cu; off += 4u) {
        wr32(STACK_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "movec %%msp,%%d7\n\t"
        "move.l #0x01ff9780,%%d0\n\t"
        "move.l #0x01ff97a0,%%d1\n\t"
        "movec %%d0,%%isp\n\t"
        "movec %%d1,%%msp\n\t"
        "move.w #0x2700,%%sr\n\t"
        "lea 0x01ff97c0,%%a0\n\t"
        "exg %%a0,%%sp\n\t"
        "move.l %%a0,0x01ff9880\n\t"
        "move.l %%sp,0x01ff9884\n\t"
        "movec %%isp,%%d2\n\t"
        "move.l %%d2,0x01ff9888\n\t"
        "move.w #0x3700,%%sr\n\t"
        "move.l %%sp,0x01ff988c\n\t"
        "lea 0x01ff97e0,%%a1\n\t"
        "exg %%a1,%%sp\n\t"
        "move.l %%a1,0x01ff9890\n\t"
        "move.l %%sp,0x01ff9894\n\t"
        "movec %%msp,%%d3\n\t"
        "move.l %%d3,0x01ff9898\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%sp,0x01ff989c\n\t"
        "move.l %%a2,%%sp\n\t"
        "movec %%d7,%%msp"
        :
        :
        : "a0", "a1", "a2", "d0", "d1", "d2", "d3", "d7", "cc", "memory");
    chk32(0x000910e0u, rd32(STACK_TEST_BASE + 0x180u), STACK_TEST_BASE + 0x80u);
    chk32(0x000910e4u, rd32(STACK_TEST_BASE + 0x184u), STACK_TEST_BASE + 0xc0u);
    chk32(0x000910e8u, rd32(STACK_TEST_BASE + 0x188u), STACK_TEST_BASE + 0xc0u);
    chk32(0x000910ecu, rd32(STACK_TEST_BASE + 0x18cu), STACK_TEST_BASE + 0xa0u);
    chk32(0x000910f0u, rd32(STACK_TEST_BASE + 0x190u), STACK_TEST_BASE + 0xa0u);
    chk32(0x000910f4u, rd32(STACK_TEST_BASE + 0x194u), STACK_TEST_BASE + 0xe0u);
    chk32(0x000910f8u, rd32(STACK_TEST_BASE + 0x198u), STACK_TEST_BASE + 0xe0u);
    chk32(0x000910fcu, rd32(STACK_TEST_BASE + 0x19cu), STACK_TEST_BASE + 0xc0u);

    wr32(STACK_TEST_BASE + 0xf4u, 0xaaaaaaaau);
    wr32(STACK_TEST_BASE + 0xf8u, 0xbbbbbbbbu);
    __asm__ volatile(
        "lea 0x01ff9764,%%a0\n\t"
        "lea 0x01ff97fc,%%a1\n\t"
        "move.l %%a0,-(%%a1)\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "memory");
    chk32(0x000910a4u, got0, STACK_TEST_BASE + 0x64u);
    chk32(0x000910a8u, got1, STACK_TEST_BASE + 0xf8u);
    chk32(0x000910acu, rd32(STACK_TEST_BASE + 0xf8u), STACK_TEST_BASE + 0x64u);

    __asm__ volatile(
        "lea 0x01ff97f8,%%a0\n\t"
        "move.l %%a0,-(%%a0)\n\t"
        "move.l %%a0,%0"
        : "=&d"(got0)
        :
        : "a0", "memory");
    chk32(0x000910b0u, got0, STACK_TEST_BASE + 0xf4u);
    chk32(0x000910b4u, rd32(STACK_TEST_BASE + 0xf4u), STACK_TEST_BASE + 0xf8u);
    chk32(0x000910b8u, rd32(STACK_TEST_BASE + 0xf8u), STACK_TEST_BASE + 0x64u);
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
    chk32(0x00092000u, got0, ADDR_TEST_BASE - 2u);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "suba.w #-4,%%a0\n\t"
        "move.l %%a0,%0"
        : "=&d"(got0)
        :
        : "a0");
    chk32(0x00092004u, got0, ADDR_TEST_BASE + 4u);

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
    chk32(0x00092008u, got0, ADDR_TEST_BASE + 8u);
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
    chk32(0x00092010u, got0, ADDR_TEST_BASE - 1u);
    chk32(0x00092014u, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "addq.l #8,%%a0\n\t"
        "subq.l #8,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc");
    chk32(0x00092018u, got0, ADDR_TEST_BASE);
    chk32(0x0009201cu, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "move.l #0x12348000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "adda.w %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x00092020u, got0, ADDR_TEST_BASE - 0x8000u);
    chk32(0x00092024u, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "move.l #0x98768000,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "suba.w %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x00092028u, got0, ADDR_TEST_BASE + 0x8000u);
    chk32(0x0009202cu, got1 & 0x1fu, 0x1fu);

    *(volatile uint16_t *)(ADDR_TEST_BASE + 0x20u) = 0xfffeu;
    *(volatile uint16_t *)(ADDR_TEST_BASE + 0x22u) = 0x0002u;
    wr32(ADDR_TEST_BASE + 0x24u, 0xfffffff0u);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "lea 0x01ffa420,%%a1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "adda.w (%%a1),%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00092030u, got0, ADDR_TEST_BASE - 2u);
    chk32(0x00092034u, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "lea 0x01ffa422,%%a1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "suba.w (%%a1),%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00092038u, got0, ADDR_TEST_BASE - 2u);
    chk32(0x0009203cu, got1 & 0x1fu, 0x1fu);

    __asm__ volatile(
        "lea 0x01ffa400,%%a0\n\t"
        "lea 0x01ffa424,%%a1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "suba.l (%%a1),%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00092040u, got0, ADDR_TEST_BASE + 0x10u);
    chk32(0x00092044u, got1 & 0x1fu, 0x1fu);
}

static void test_register_transform_directed(void)
{
    uint32_t got0;
    uint32_t got1;

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #-128,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093070u, got0, 0xffffff80u);
    chk32(0x00093074u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #0,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093078u, got0, 0u);
    chk32(0x0009307cu, got1 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #127,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x00093080u, got0, 0x0000007fu);
    chk32(0x00093084u, got1 & 0x1fu, 0x10u);

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
        "movea.l #0x00007fff,%%a0\n\t"
        "move.l #0x12348000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x00093090u, got0 & 0x1fu, 0x11u);

    __asm__ volatile(
        "movea.l #0xffff8000,%%a0\n\t"
        "move.l #0x56788000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x00093094u, got0 & 0x1fu, 0x14u);

    __asm__ volatile(
        "movea.l #0x7fffffff,%%a0\n\t"
        "move.l #-1,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.l %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x00093098u, got0 & 0x1fu, 0x1bu);

    __asm__ volatile(
        "movea.l #0x00008000,%%a0\n\t"
        "move.l #0x12347fff,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w %%d0,%%a0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d1", "cc");
    chk32(0x000930b0u, got0 & 0x1fu, 0x10u);

    *(volatile uint16_t *)(ADDR_TEST_BASE + 0x30u) = 0x8000u;
    wr32(ADDR_TEST_BASE + 0x34u, 0xffffffffu);
    wr32(ADDR_TEST_BASE + 0x38u, 0x00000001u);

    __asm__ volatile(
        "movea.l #0x7fffffff,%%a0\n\t"
        "lea 0x01ffa430,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.w (%%a1)+,%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a1,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x0009309cu, got0, ADDR_TEST_BASE + 0x32u);
    chk32(0x000930a0u, got1 & 0x1fu, 0x1bu);

    __asm__ volatile(
        "movea.l #0x00000000,%%a0\n\t"
        "lea 0x01ffa438,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.l -(%%a1),%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a1,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x000930a4u, got0, ADDR_TEST_BASE + 0x34u);
    chk32(0x000930a8u, got1 & 0x1fu, 0x11u);

    __asm__ volatile(
        "movea.l #0x80000000,%%a0\n\t"
        "lea 0x01ffa438,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpa.l (%%a1),%%a0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x000930acu, got0 & 0x1fu, 0x12u);

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

    wr32(ADDR_TEST_BASE + 0x40u, 0xa5008000u);
    __asm__ volatile(
        "move.l #0x12345678,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "move.b 0x01ffa440,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000930c0u, got0, 0x123456a5u);
    chk32(0x000930c4u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x12345678,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "move.w 0x01ffa442,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000930c8u, got0, 0x12348000u);
    chk32(0x000930ccu, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0xffffffff,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "move.b #0,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "cc");
    chk32(0x000930d0u, got0, 0xffffff00u);
    chk32(0x000930d4u, got1 & 0x1fu, 0x14u);
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
        "move.l #0x0f0f0f0f,%%d0\n\t"
        "move.l #0xf0f0f0f0,%%d1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "eor.l %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00094080u, got0, 0x0f0f0f0fu);
    chk32(0x00094084u, got1, 0xffffffffu);
    chk32(0x00094088u, got2 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x000000aa,%%d0\n\t"
        "move.l #0x123456aa,%%d1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "eor.b %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x0009408cu, got0, 0x12345600u);
    chk32(0x00094090u, got1 & 0x1fu, 0x14u);

    wr32(UNARY_TEST_BASE + 0x14u, 0x123455aau);
    __asm__ volatile(
        "move.l #0x0000ff00,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "eor.w %%d0,0x01ffa516\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,%0"
        : "=&d"(got0)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x00094094u, rd32(UNARY_TEST_BASE + 0x14u), 0x1234aaaau);
    chk32(0x00094098u, got0 & 0x1fu, 0x18u);

    *(volatile uint8_t *)(UNARY_TEST_BASE + 0x21u) = 0xf0u;
    __asm__ volatile(
        "lea 0x01ffa521,%%a0\n\t"
        "moveq #0x0f,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "eor.b %%d0,(%%a0)+\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x0009409cu, got0, UNARY_TEST_BASE + 0x22u);
    chk8(0x000940a0u, rd8(UNARY_TEST_BASE + 0x21u), 0xffu);
    chk32(0x000940a4u, got1 & 0x1fu, 0x18u);

    __asm__ volatile(
        "move.l #0x0000000f,%%d0\n\t"
        "move.l #0x123456f0,%%d1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "and.b %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x000940a8u, got0, 0x12345600u);
    chk32(0x000940acu, got1 & 0x1fu, 0x14u);

    __asm__ volatile(
        "move.l #0x00000080,%%d0\n\t"
        "move.l #0x12345600,%%d1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "or.b %%d0,%%d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x000940b0u, got0, 0x12345680u);
    chk32(0x000940b4u, got1 & 0x1fu, 0x18u);

    wr32(UNARY_TEST_BASE + 0x24u, 0xaa55cc33u);
    wr32(UNARY_TEST_BASE + 0x28u, 0u);
    wr32(UNARY_TEST_BASE + 0x2cu, 0u);
    __asm__ volatile(
        "lea 0x01ffa524,%%a0\n\t"
        "moveq #0x0f,%%d0\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "and.b %%d0,1(%%a0)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l #0x80,%%d0\n\t"
        "or.b %%d0,3(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d1,0x01ffa528\n\t"
        "move.l %%d2,0x01ffa52c"
        :
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk32(0x000940b8u, rd32(UNARY_TEST_BASE + 0x24u), 0xaa05ccb3u);
    chk32(0x000940bcu, rd32(UNARY_TEST_BASE + 0x28u) & 0x1fu, 0x10u);
    chk32(0x000940c0u, rd32(UNARY_TEST_BASE + 0x2cu) & 0x1fu, 0x18u);

    wr32(UNARY_TEST_BASE + 0x40u, 0x12345678u);
    wr32(UNARY_TEST_BASE + 0x44u, 0x80000001u);
    wr32(UNARY_TEST_BASE + 0x48u, 0x0f0f00f0u);
    wr32(UNARY_TEST_BASE + 0x4cu, 0x00ff5500u);
    wr32(UNARY_TEST_BASE + 0x50u, 0xf0f00ff0u);
    wr32(UNARY_TEST_BASE + 0x54u, 0x00008000u);
    wr32(UNARY_TEST_BASE + 0x58u, 0xaaaaaaaau);
    for (uint32_t off = 0x60u; off <= 0x78u; off += 4u) {
        wr32(UNARY_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #2,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "not.w 0x01ffa540(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa560\n\t"
        "moveq #4,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "neg.l 0x01ffa540(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa564\n\t"
        "movea.l #9,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "clr.b 0x01ffa540(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa568\n\t"
        "moveq #6,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "tst.w 0x01ffa540(%%d4:l:2)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa56c\n\t"
        "moveq #0x0f,%%d0\n\t"
        "moveq #3,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "and.b %%d0,0x01ffa550(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa570\n\t"
        "move.l #0x00000f0f,%%d0\n\t"
        "move.l #0x7fff0004,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "or.w %%d0,0x01ffa550(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa574\n\t"
        "moveq #-1,%%d0\n\t"
        "movea.l #0,%%a1\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "eor.l %%d0,0x01ffa558(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa578"
        :
        :
        : "a1", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000940c4u, rd32(UNARY_TEST_BASE + 0x40u), 0x1234a987u);
    chk32(0x000940c8u, rd32(UNARY_TEST_BASE + 0x44u), 0x7fffffffu);
    chk32(0x000940ccu, rd32(UNARY_TEST_BASE + 0x48u), 0x0f0000f0u);
    chk32(0x000940d0u, rd32(UNARY_TEST_BASE + 0x4cu), 0x00ff5500u);
    chk32(0x000940d4u, rd32(UNARY_TEST_BASE + 0x50u), 0xf0f00f00u);
    chk32(0x000940d8u, rd32(UNARY_TEST_BASE + 0x54u), 0x0f0f8000u);
    chk32(0x000940dcu, rd32(UNARY_TEST_BASE + 0x58u), 0x55555555u);
    chk32(0x000940e0u, rd32(UNARY_TEST_BASE + 0x60u) & 0x1fu, 0x18u);
    chk32(0x000940e4u, rd32(UNARY_TEST_BASE + 0x64u) & 0x1fu, 0x11u);
    chk32(0x000940e8u, rd32(UNARY_TEST_BASE + 0x68u) & 0x1fu, 0x14u);
    chk32(0x000940ecu, rd32(UNARY_TEST_BASE + 0x6cu) & 0x1fu, 0x10u);
    chk32(0x000940f0u, rd32(UNARY_TEST_BASE + 0x70u) & 0x1fu, 0x14u);
    chk32(0x000940f4u, rd32(UNARY_TEST_BASE + 0x74u) & 0x1fu, 0x10u);
    chk32(0x000940f8u, rd32(UNARY_TEST_BASE + 0x78u) & 0x1fu, 0x10u);

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

    *(volatile uint8_t *)(IMM_TEST_BASE + 0x31u) = 0x7fu;
    __asm__ volatile(
        "lea 0x01ffa631,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "addq.b #1,(%%a0)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc", "memory");
    chk32(0x00095048u, got0, IMM_TEST_BASE + 0x32u);
    chk8(0x0009504cu, rd8(IMM_TEST_BASE + 0x31u), 0x80u);
    chk32(0x00095050u, got1 & 0x1fu, 0x0au);

    *(volatile uint16_t *)(IMM_TEST_BASE + 0x42u) = 0x0000u;
    __asm__ volatile(
        "lea 0x01ffa642,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "subq.w #1,(%%a0)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc", "memory");
    chk32(0x00095054u, got0, IMM_TEST_BASE + 0x44u);
    chk16(0x00095058u, rd16(IMM_TEST_BASE + 0x42u), 0xffffu);
    chk32(0x0009505cu, got1 & 0x1fu, 0x19u);

    wr32(IMM_TEST_BASE + 0x50u, 0x00000003u);
    __asm__ volatile(
        "lea 0x01ffa650,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "addq.l #5,(%%a0)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "d0", "cc", "memory");
    chk32(0x00095060u, got0, IMM_TEST_BASE + 0x54u);
    chk32(0x00095064u, rd32(IMM_TEST_BASE + 0x50u), 0x00000008u);
    chk32(0x00095068u, got1 & 0x1fu, 0x00u);

    for (uint32_t off = 0x60u; off <= 0x70u; off += 4u) {
        wr32(IMM_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l #0x11111111,%%d0\n\t"
        "addi.l #0x22222222,%%d0\n\t"
        "move.l %%d0,0x01ffa660\n\t"
        "move.l #0x12340000,%%d1\n\t"
        "subi.w #1,%%d1\n\t"
        "move.l %%d1,0x01ffa664\n\t"
        "move.l #0xaaaa12f5,%%d2\n\t"
        "andi.b #0x0f,%%d2\n\t"
        "move.l %%d2,0x01ffa668\n\t"
        "move.l #0x5555500f,%%d3\n\t"
        "ori.w #0x0f00,%%d3\n\t"
        "move.l %%d3,0x01ffa66c\n\t"
        "move.l #0x0f0f0f0f,%%d4\n\t"
        "eori.l #0xffffffff,%%d4\n\t"
        "move.l %%d4,0x01ffa670"
        :
        :
        : "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x0009506cu, rd32(IMM_TEST_BASE + 0x60u), 0x33333333u);
    chk32(0x00095070u, rd32(IMM_TEST_BASE + 0x64u), 0x1234ffffu);
    chk32(0x00095074u, rd32(IMM_TEST_BASE + 0x68u), 0xaaaa1205u);
    chk32(0x00095078u, rd32(IMM_TEST_BASE + 0x6cu), 0x55555f0fu);
    chk32(0x0009507cu, rd32(IMM_TEST_BASE + 0x70u), 0xf0f0f0f0u);

    wr32(IMM_TEST_BASE + 0x80u, 0x127f8000u);
    wr32(IMM_TEST_BASE + 0x84u, 0x00000001u);
    wr32(IMM_TEST_BASE + 0x88u, 0xffff0f0fu);
    wr32(IMM_TEST_BASE + 0x8cu, 0x12345678u);
    wr32(IMM_TEST_BASE + 0x90u, 0x00000010u);
    wr32(IMM_TEST_BASE + 0x94u, 0x12340000u);
    for (uint32_t off = 0xa0u; off <= 0xb4u; off += 4u) {
        wr32(IMM_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #1,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "addi.b #1,0x01ffa680(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6a0\n\t"
        "moveq #6,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "subi.w #1,0x01ffa680(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6a4\n\t"
        "moveq #10,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "andi.w #0x00f0,0x01ffa680(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6a8\n\t"
        "move.l #0x7fff000c,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "eori.l #0xffffffff,0x01ffa680(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6ac\n\t"
        "movea.l #16,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmpi.l #0x00000020,0x01ffa680(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6b0\n\t"
        "move.l #0x7fff0016,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "ori.w #0x0f0f,0x01ffa680(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa6b4"
        :
        :
        : "a1", "d1", "d4", "cc", "memory");
    chk32(0x00095080u, rd32(IMM_TEST_BASE + 0x80u), 0x12808000u);
    chk32(0x00095084u, rd32(IMM_TEST_BASE + 0x84u), 0x00000000u);
    chk32(0x00095088u, rd32(IMM_TEST_BASE + 0x88u), 0xffff0000u);
    chk32(0x0009508cu, rd32(IMM_TEST_BASE + 0x8cu), 0xedcba987u);
    chk32(0x00095090u, rd32(IMM_TEST_BASE + 0x90u), 0x00000010u);
    chk32(0x00095094u, rd32(IMM_TEST_BASE + 0x94u), 0x12340f0fu);
    chk32(0x00095098u, rd32(IMM_TEST_BASE + 0xa0u) & 0x1fu, 0x0au);
    chk32(0x0009509cu, rd32(IMM_TEST_BASE + 0xa4u) & 0x1fu, 0x04u);
    chk32(0x000950a0u, rd32(IMM_TEST_BASE + 0xa8u) & 0x1fu, 0x14u);
    chk32(0x000950a4u, rd32(IMM_TEST_BASE + 0xacu) & 0x1fu, 0x18u);
    chk32(0x000950a8u, rd32(IMM_TEST_BASE + 0xb0u) & 0x1fu, 0x19u);
    chk32(0x000950acu, rd32(IMM_TEST_BASE + 0xb4u) & 0x1fu, 0x10u);
}

static void test_data_alu_indexed_directed(void)
{
    wr32(DATA_ALU_TEST_BASE + 0x00u, 0x127f8000u);
    wr32(DATA_ALU_TEST_BASE + 0x04u, 0xaaaa0001u);
    wr32(DATA_ALU_TEST_BASE + 0x08u, 0x7fffffffu);
    wr32(DATA_ALU_TEST_BASE + 0x0cu, 0x12345600u);
    wr32(DATA_ALU_TEST_BASE + 0x10u, 0x55558000u);
    wr32(DATA_ALU_TEST_BASE + 0x14u, 0x80000000u);
    wr32(DATA_ALU_TEST_BASE + 0x18u, 0x00aa80bbu);
    wr32(DATA_ALU_TEST_BASE + 0x1cu, 0xaaaa7fffu);
    wr32(DATA_ALU_TEST_BASE + 0x20u, 0x12345678u);
    for (uint32_t off = 0x80u; off <= 0xa0u; off += 4u) {
        wr32(DATA_ALU_TEST_BASE + off, 0u);
    }

    __asm__ volatile(
        "moveq #1,%%d0\n\t"
        "moveq #1,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "add.b %%d0,0x01ffb000(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb080\n\t"
        "moveq #-1,%%d0\n\t"
        "moveq #6,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "add.w %%d0,0x01ffb000(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb084\n\t"
        "moveq #1,%%d0\n\t"
        "movea.l #8,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "add.l %%d0,0x01ffb000(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb088\n\t"
        "moveq #1,%%d0\n\t"
        "move.l #0x7fff000f,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "sub.b %%d0,0x01ffb000(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb08c\n\t"
        "moveq #1,%%d0\n\t"
        "moveq #9,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "sub.w %%d0,0x01ffb000(%%d4:l:2)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb090\n\t"
        "moveq #1,%%d0\n\t"
        "movea.l #20,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "sub.l %%d0,0x01ffb000(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb094\n\t"
        "move.l #0x0000007f,%%d0\n\t"
        "move.l #0x0000001a,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmp.b 0x01ffb000(%%d4:l),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb098\n\t"
        "move.l #0x00008000,%%d0\n\t"
        "moveq #-1,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmp.w 0x01ffb01f(%%d4:w),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb09c\n\t"
        "move.l #0x12345678,%%d0\n\t"
        "movea.l #0,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cmp.l 0x01ffb020(%%a1:l),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffb0a0"
        :
        :
        : "a1", "d0", "d1", "d4", "cc", "memory");

    chk32(0x00095100u, rd32(DATA_ALU_TEST_BASE + 0x00u), 0x12808000u);
    chk32(0x00095104u, rd32(DATA_ALU_TEST_BASE + 0x04u), 0xaaaa0000u);
    chk32(0x00095108u, rd32(DATA_ALU_TEST_BASE + 0x08u), 0x80000000u);
    chk32(0x0009510cu, rd32(DATA_ALU_TEST_BASE + 0x0cu), 0x123456ffu);
    chk32(0x00095110u, rd32(DATA_ALU_TEST_BASE + 0x10u), 0x55557fffu);
    chk32(0x00095114u, rd32(DATA_ALU_TEST_BASE + 0x14u), 0x7fffffffu);
    chk32(0x00095118u, rd32(DATA_ALU_TEST_BASE + 0x18u), 0x00aa80bbu);
    chk32(0x0009511cu, rd32(DATA_ALU_TEST_BASE + 0x1cu), 0xaaaa7fffu);
    chk32(0x00095120u, rd32(DATA_ALU_TEST_BASE + 0x20u), 0x12345678u);
    chk32(0x00095124u, rd32(DATA_ALU_TEST_BASE + 0x80u) & 0x1fu, 0x0au);
    chk32(0x00095128u, rd32(DATA_ALU_TEST_BASE + 0x84u) & 0x1fu, 0x15u);
    chk32(0x0009512cu, rd32(DATA_ALU_TEST_BASE + 0x88u) & 0x1fu, 0x0au);
    chk32(0x00095130u, rd32(DATA_ALU_TEST_BASE + 0x8cu) & 0x1fu, 0x19u);
    chk32(0x00095134u, rd32(DATA_ALU_TEST_BASE + 0x90u) & 0x1fu, 0x02u);
    chk32(0x00095138u, rd32(DATA_ALU_TEST_BASE + 0x94u) & 0x1fu, 0x02u);
    chk32(0x0009513cu, rd32(DATA_ALU_TEST_BASE + 0x98u) & 0x1fu, 0x1bu);
    chk32(0x00095140u, rd32(DATA_ALU_TEST_BASE + 0x9cu) & 0x1fu, 0x12u);
    chk32(0x00095144u, rd32(DATA_ALU_TEST_BASE + 0xa0u) & 0x1fu, 0x14u);
}

static uint32_t alu_size_mask(uint32_t bits)
{
    return bits == 32u ? 0xffffffffu : ((1u << bits) - 1u);
}

static uint32_t add_ccr_ref(uint32_t bits, uint32_t src, uint32_t dst,
                            uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t srcm = src & mask;
    uint32_t dstm = dst & mask;
    uint32_t sum = srcm + dstm;
    uint32_t res = sum & mask;
    uint32_t c = bits == 32u ? (sum < srcm) : ((sum & ~mask) != 0u);
    uint32_t v = ((~(srcm ^ dstm) & (res ^ dstm) & sign) != 0u);
    uint32_t n = (res & sign) != 0u;
    uint32_t z = res == 0u;

    *result = (dst & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

static uint32_t sub_ccr_ref(uint32_t bits, uint32_t src, uint32_t dst,
                            uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t srcm = src & mask;
    uint32_t dstm = dst & mask;
    uint32_t res = (dstm - srcm) & mask;
    uint32_t c = srcm > dstm;
    uint32_t v = (((dstm ^ srcm) & (res ^ dstm) & sign) != 0u);
    uint32_t n = (res & sign) != 0u;
    uint32_t z = res == 0u;

    *result = (dst & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

static uint32_t cmp_ccr_ref(uint32_t bits, uint32_t src, uint32_t dst)
{
    uint32_t unused;
    return 0x10u | (sub_ccr_ref(bits, src, dst, &unused) & 0x0fu);
}

static void cpu_add_reg(uint32_t bits, uint32_t src, uint32_t dst,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "add.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "add.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "add.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void cpu_sub_reg(uint32_t bits, uint32_t src, uint32_t dst,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "sub.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "sub.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "sub.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void cpu_cmp_reg(uint32_t bits, uint32_t src, uint32_t dst,
                        uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %1,%%d0\n\t"
            "move.l %2,%%d1\n\t"
            "move.w #0x10,%%ccr\n\t"
            "cmp.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d2,%0"
            : "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %1,%%d0\n\t"
            "move.l %2,%%d1\n\t"
            "move.w #0x10,%%ccr\n\t"
            "cmp.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d2,%0"
            : "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %1,%%d0\n\t"
            "move.l %2,%%d1\n\t"
            "move.w #0x10,%%ccr\n\t"
            "cmp.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d2,%0"
            : "=m"(*ccr)
            : "d"(src), "d"(dst)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void test_data_alu_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } pairs[] = {
        {0x00000000u, 0x00000000u},
        {0x00000001u, 0x00000000u},
        {0x00000001u, 0x0000007fu},
        {0x00000001u, 0x000000ffu},
        {0x000000ffu, 0x00000001u},
        {0x00000080u, 0x00000080u},
        {0x00007fffu, 0x00000001u},
        {0x00008000u, 0x00008000u},
        {0x0000ffffu, 0x00000001u},
        {0xffffffffu, 0x00000001u},
        {0x80000000u, 0x80000000u},
        {0x7fffffffu, 0x00000001u},
        {0x12345678u, 0x89abcdefu},
        {0x89abcdefu, 0x76543210u},
    };

    for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
        for (uint32_t i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); ++i) {
            uint32_t id = 0x00170000u + (s << 12) + (i << 5);
            uint32_t bits = sizes[s];
            uint32_t got_result;
            uint32_t got_ccr;
            uint32_t exp_result;
            uint32_t exp_ccr;

            cpu_add_reg(bits, pairs[i].src, pairs[i].dst, &got_result, &got_ccr);
            exp_ccr = add_ccr_ref(bits, pairs[i].src, pairs[i].dst, &exp_result);
            chk32(id + 0x00u, got_result, exp_result);
            chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);

            cpu_sub_reg(bits, pairs[i].src, pairs[i].dst, &got_result, &got_ccr);
            exp_ccr = sub_ccr_ref(bits, pairs[i].src, pairs[i].dst, &exp_result);
            chk32(id + 0x08u, got_result, exp_result);
            chk32(id + 0x0cu, got_ccr & 0x1fu, exp_ccr);

            cpu_cmp_reg(bits, pairs[i].src, pairs[i].dst, &got_ccr);
            chk32(id + 0x10u, got_ccr & 0x1fu,
                  cmp_ccr_ref(bits, pairs[i].src, pairs[i].dst));
        }
    }

    mark(0x0017f000u, 0xadd50000u);
}

static uint32_t addx_ccr_ref(uint32_t bits, uint32_t src, uint32_t dst,
                             uint32_t initial_x, uint32_t initial_z,
                             uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t srcm = src & mask;
    uint32_t dstm = dst & mask;
    uint32_t x = initial_x != 0u;
    uint32_t sum;
    uint32_t res;
    uint32_t c;
    uint32_t v;
    uint32_t n;
    uint32_t z;

    if (bits == 32u) {
        uint32_t sum0 = srcm + dstm;
        uint32_t sum1 = sum0 + x;

        c = (sum0 < srcm) || (x && sum1 < sum0);
        res = sum1;
    } else {
        sum = srcm + dstm + x;
        c = (sum & ~mask) != 0u;
        res = sum & mask;
    }

    v = (~(srcm ^ dstm) & (res ^ dstm) & sign) != 0u;
    n = (res & sign) != 0u;
    z = res == 0u ? (initial_z != 0u) : 0u;

    *result = (dst & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

static uint32_t subx_ccr_ref(uint32_t bits, uint32_t src, uint32_t dst,
                             uint32_t initial_x, uint32_t initial_z,
                             uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t srcm = src & mask;
    uint32_t dstm = dst & mask;
    uint32_t x = initial_x != 0u;
    uint32_t subtr;
    uint32_t res;
    uint32_t c;
    uint32_t v;
    uint32_t n;
    uint32_t z;

    if (bits == 32u) {
        subtr = srcm + x;
        c = (x && subtr == 0u) || (subtr > dstm);
    } else {
        subtr = srcm + x;
        c = subtr > dstm;
    }
    res = (dstm - subtr) & mask;

    v = ((dstm ^ srcm) & (res ^ dstm) & sign) != 0u;
    n = (res & sign) != 0u;
    z = res == 0u ? (initial_z != 0u) : 0u;

    *result = (dst & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

#define CPU_XALU_REG(OP, SIZE)                                                 \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.l %4,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP "." #SIZE " %%d0,%%d1\n\t"                                     \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d1,%0\n\t"                                               \
            "move.l %%d2,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_xalu_reg(uint32_t op, uint32_t bits, uint32_t src,
                         uint32_t dst, uint32_t initial_x,
                         uint32_t initial_z, uint32_t *result,
                         uint32_t *ccr)
{
    uint32_t initial_ccr = (initial_x ? 0x10u : 0u) |
                           (initial_z ? 0x04u : 0u);

    if (op == 0u) {
        switch (bits) {
        case 8u:
            CPU_XALU_REG(addx, b);
            break;
        case 16u:
            CPU_XALU_REG(addx, w);
            break;
        default:
            CPU_XALU_REG(addx, l);
            break;
        }
    } else {
        switch (bits) {
        case 8u:
            CPU_XALU_REG(subx, b);
            break;
        case 16u:
            CPU_XALU_REG(subx, w);
            break;
        default:
            CPU_XALU_REG(subx, l);
            break;
        }
    }
}

#undef CPU_XALU_REG

static void test_xalu_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } pairs[] = {
        {0x00000000u, 0x00000000u},
        {0x00000000u, 0x00000001u},
        {0x00000001u, 0x00000000u},
        {0x00000001u, 0x0000007fu},
        {0x000000ffu, 0x00000001u},
        {0x00000080u, 0x00000080u},
        {0x0000ffffu, 0x00000001u},
        {0x00007fffu, 0x00000001u},
        {0x00008000u, 0x00008000u},
        {0xffffffffu, 0x00000001u},
        {0x7fffffffu, 0x00000001u},
        {0x80000000u, 0x80000000u},
        {0x12345678u, 0x89abcdefu},
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
            uint32_t bits = sizes[s];

            for (uint32_t xi = 0; xi < 2u; ++xi) {
                for (uint32_t zi = 0; zi < 2u; ++zi) {
                    for (uint32_t i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); ++i) {
                        uint32_t id = 0x001e0000u + (op << 15) + (s << 12) +
                                      (xi << 11) + (zi << 10) + (i << 4);
                        uint32_t got_result;
                        uint32_t got_ccr;
                        uint32_t exp_result;
                        uint32_t exp_ccr;

                        cpu_xalu_reg(op, bits, pairs[i].src, pairs[i].dst, xi, zi,
                                     &got_result, &got_ccr);
                        if (op == 0u) {
                            exp_ccr = addx_ccr_ref(bits, pairs[i].src, pairs[i].dst,
                                                   xi, zi, &exp_result);
                        } else {
                            exp_ccr = subx_ccr_ref(bits, pairs[i].src, pairs[i].dst,
                                                   xi, zi, &exp_result);
                        }
                        chk32(id + 0x00u, got_result, exp_result);
                        chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                    }
                }
            }
        }
    }

    mark(0x001ef000u, 0xad5b0000u);
}

static uint32_t neg_ccr_ref(uint32_t bits, uint32_t value, uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t val = value & mask;
    uint32_t res = (0u - val) & mask;
    uint32_t c = val != 0u;
    uint32_t v = val == sign;
    uint32_t n = (res & sign) != 0u;
    uint32_t z = res == 0u;

    *result = (value & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

static uint32_t negx_ccr_ref(uint32_t bits, uint32_t value,
                             uint32_t initial_x, uint32_t initial_z,
                             uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t val = value & mask;
    uint32_t x = initial_x != 0u;
    uint32_t subtr = val + x;
    uint32_t res = (0u - subtr) & mask;
    uint32_t c = val != 0u || x;
    uint32_t v = (val & sign) != 0u && (res & sign) != 0u;
    uint32_t n = (res & sign) != 0u;
    uint32_t z = res == 0u ? (initial_z != 0u) : 0u;

    *result = (value & ~mask) | res;
    return (c ? 0x11u : 0u) | (n ? 0x08u : 0u) |
           (z ? 0x04u : 0u) | (v ? 0x02u : 0u);
}

#define CPU_UNARY_ARITH_REG(OP, SIZE)                                          \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.w %%d1,%%ccr\n\t"                                            \
            #OP "." #SIZE " %%d0\n\t"                                          \
            "move.w %%sr,%%d1\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(value), "d"(initial_ccr)                                     \
            : "d0", "d1", "cc", "memory");                                   \
    } while (0)

static void cpu_neg_reg(uint32_t bits, uint32_t value, uint32_t initial_ccr,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        CPU_UNARY_ARITH_REG(neg, b);
        break;
    case 16u:
        CPU_UNARY_ARITH_REG(neg, w);
        break;
    default:
        CPU_UNARY_ARITH_REG(neg, l);
        break;
    }
}

static void cpu_negx_reg(uint32_t bits, uint32_t value, uint32_t initial_ccr,
                         uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        CPU_UNARY_ARITH_REG(negx, b);
        break;
    case 16u:
        CPU_UNARY_ARITH_REG(negx, w);
        break;
    default:
        CPU_UNARY_ARITH_REG(negx, l);
        break;
    }
}

#undef CPU_UNARY_ARITH_REG

static void test_unary_arith_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const uint32_t ccrs[] = {0x00u, 0x04u, 0x10u, 0x14u, 0x1fu};
    static const uint32_t values[] = {
        0x00000000u, 0x00000001u, 0x0000007fu, 0x00000080u,
        0x000000ffu, 0x00007fffu, 0x00008000u, 0x0000ffffu,
        0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u,
        0x89abcdefu,
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
            uint32_t bits = sizes[s];

            for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
                for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
                    uint32_t id = 0x00220000u + (op << 14) + (s << 11) +
                                  (ci << 7) + (vi << 3);
                    uint32_t got_result;
                    uint32_t got_ccr;
                    uint32_t exp_result;
                    uint32_t exp_ccr;

#ifdef CORETEST_SIM_TRACE_UNARY_ARITH
                    uart_puts("UA ");
                    uart_hex32(id);
                    uart_putc('\n');
#endif
                    if (op == 0u) {
                        cpu_neg_reg(bits, values[vi], ccrs[ci],
                                    &got_result, &got_ccr);
                        exp_ccr = neg_ccr_ref(bits, values[vi], &exp_result);
                    } else {
                        cpu_negx_reg(bits, values[vi], ccrs[ci],
                                     &got_result, &got_ccr);
                        exp_ccr = negx_ccr_ref(bits, values[vi],
                                               ccrs[ci] & 0x10u,
                                               ccrs[ci] & 0x04u, &exp_result);
                    }

                    chk32(id + 0x00u, got_result, exp_result);
                    chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                }
            }
        }
    }

    mark(0x0022f000u, 0x0e600000u);
}

static uint32_t unary_logic_ccr_ref(uint32_t op, uint32_t bits, uint32_t value,
                                    uint32_t initial_ccr, uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t res;

    if (op == 0u) {
        res = 0u;
    } else if (op == 1u) {
        res = (~value) & mask;
    } else {
        res = value & mask;
    }

    if (op == 2u) {
        *result = value;
    } else {
        *result = (value & ~mask) | res;
    }

    return (initial_ccr & 0x10u) | ((res & sign) ? 0x08u : 0u) |
           (res == 0u ? 0x04u : 0u);
}

#define CPU_UNARY_LOGIC_REG(OP, SIZE)                                          \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.w %%d1,%%ccr\n\t"                                            \
            #OP "." #SIZE " %%d0\n\t"                                          \
            "move.w %%sr,%%d1\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(value), "d"(initial_ccr)                                     \
            : "d0", "d1", "cc", "memory");                                   \
    } while (0)

static void cpu_unary_logic_reg(uint32_t op, uint32_t bits, uint32_t value,
                                uint32_t initial_ccr, uint32_t *result,
                                uint32_t *ccr)
{
    if (op == 0u) {
        switch (bits) {
        case 8u:
            CPU_UNARY_LOGIC_REG(clr, b);
            break;
        case 16u:
            CPU_UNARY_LOGIC_REG(clr, w);
            break;
        default:
            CPU_UNARY_LOGIC_REG(clr, l);
            break;
        }
    } else if (op == 1u) {
        switch (bits) {
        case 8u:
            CPU_UNARY_LOGIC_REG(not, b);
            break;
        case 16u:
            CPU_UNARY_LOGIC_REG(not, w);
            break;
        default:
            CPU_UNARY_LOGIC_REG(not, l);
            break;
        }
    } else {
        switch (bits) {
        case 8u:
            CPU_UNARY_LOGIC_REG(tst, b);
            break;
        case 16u:
            CPU_UNARY_LOGIC_REG(tst, w);
            break;
        default:
            CPU_UNARY_LOGIC_REG(tst, l);
            break;
        }
    }
}

#undef CPU_UNARY_LOGIC_REG

static void test_unary_logic_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const uint32_t values[] = {
        0x00000000u, 0x00000001u, 0x0000007fu, 0x00000080u,
        0x000000ffu, 0x00007fffu, 0x00008000u, 0x0000ffffu,
        0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u,
        0x89abcdefu,
    };

    for (uint32_t op = 0; op < 3u; ++op) {
        for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
            uint32_t bits = sizes[s];

            for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
                for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
                    uint32_t id = 0x00230000u + (op << 14) + (s << 11) +
                                  (ci << 7) + (vi << 3);
                    uint32_t got_result;
                    uint32_t got_ccr;
                    uint32_t exp_result;
                    uint32_t exp_ccr;

                    cpu_unary_logic_reg(op, bits, values[vi], ccrs[ci],
                                        &got_result, &got_ccr);
                    exp_ccr = unary_logic_ccr_ref(op, bits, values[vi],
                                                  ccrs[ci], &exp_result);
                    chk32(id + 0x00u, got_result, exp_result);
                    chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                }
            }
        }
    }

    mark(0x0023f000u, 0xc1700000u);
}

static uint32_t shift_reg_ref(uint32_t op, uint32_t bits, uint32_t value,
                              uint32_t count, uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t res = value & mask;
    uint32_t c = 0u;

    count &= 63u;
    for (uint32_t i = 0; i < count; ++i) {
        if (op == 0u) {
            c = (res & sign) != 0u;
            res = (res << 1) & mask;
        } else {
            c = res & 1u;
            res >>= 1;
            if (op == 2u && (value & sign)) {
                res |= sign;
            }
        }
    }

    *result = (value & ~mask) | res;
    return (c ? 0x11u : 0u) | ((res & sign) ? 0x08u : 0u) |
           (res == 0u ? 0x04u : 0u);
}

static void cpu_lsl_reg(uint32_t bits, uint32_t count, uint32_t value,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsl.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsl.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsl.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void cpu_lsr_reg(uint32_t bits, uint32_t count, uint32_t value,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsr.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsr.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "lsr.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void cpu_asr_reg(uint32_t bits, uint32_t count, uint32_t value,
                        uint32_t *result, uint32_t *ccr)
{
    switch (bits) {
    case 8u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "asr.b %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    case 16u:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "asr.w %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    default:
        __asm__ volatile(
            "move.l %2,%%d0\n\t"
            "move.l %3,%%d1\n\t"
            "move.w #0,%%ccr\n\t"
            "asr.l %%d0,%%d1\n\t"
            "move.w %%sr,%%d2\n\t"
            "move.l %%d1,%0\n\t"
            "move.l %%d2,%1"
            : "=m"(*result), "=m"(*ccr)
            : "d"(count), "d"(value)
            : "d0", "d1", "d2", "cc", "memory");
        break;
    }
}

static void test_shift_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const uint32_t counts[] = {1u, 2u, 3u, 7u, 8u, 15u, 16u, 31u};
    static const uint32_t values[] = {
        0x00000000u, 0x00000001u, 0x0000007fu, 0x00000080u,
        0x000000ffu, 0x00007fffu, 0x00008000u, 0x0000ffffu,
        0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u,
        0x89abcdefu,
    };

    for (uint32_t op = 0; op < 3u; ++op) {
        for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
            uint32_t bits = sizes[s];

            for (uint32_t ci = 0; ci < (sizeof(counts) / sizeof(counts[0])); ++ci) {
                uint32_t count = counts[ci];

                if (count >= bits) continue;

                for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
                    uint32_t id = 0x00180000u + (op << 15) + (s << 12) +
                                  (ci << 7) + (vi << 3);
                    uint32_t got_result;
                    uint32_t got_ccr;
                    uint32_t exp_result;
                    uint32_t exp_ccr;

                    if (op == 0u) {
                        cpu_lsl_reg(bits, count, values[vi], &got_result, &got_ccr);
                    } else if (op == 1u) {
                        cpu_lsr_reg(bits, count, values[vi], &got_result, &got_ccr);
                    } else {
                        cpu_asr_reg(bits, count, values[vi], &got_result, &got_ccr);
                    }

                    exp_ccr = shift_reg_ref(op, bits, values[vi], count, &exp_result);
                    chk32(id + 0x00u, got_result, exp_result);
                    chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                }
            }
        }
    }

    mark(0x0018f000u, 0x51f70000u);
}

static uint32_t rotate_reg_ref(uint32_t op, uint32_t bits, uint32_t value,
                               uint32_t count, uint32_t initial_x,
                               uint32_t *result)
{
    uint32_t mask = alu_size_mask(bits);
    uint32_t sign = 1u << (bits - 1u);
    uint32_t res = value & mask;
    uint32_t x = initial_x != 0u;
    uint32_t c = 0u;

    count &= 63u;
    for (uint32_t i = 0; i < count; ++i) {
        if (op == 0u) {
            c = (res & sign) != 0u;
            res = ((res << 1) & mask) | c;
        } else if (op == 1u) {
            c = res & 1u;
            res = (res >> 1) | (c ? sign : 0u);
        } else if (op == 2u) {
            c = (res & sign) != 0u;
            res = ((res << 1) & mask) | x;
            x = c;
        } else {
            c = res & 1u;
            res = (res >> 1) | (x ? sign : 0u);
            x = c;
        }
    }

    *result = (value & ~mask) | res;
    return (x ? 0x10u : 0u) | (c ? 0x01u : 0u) |
           ((res & sign) ? 0x08u : 0u) | (res == 0u ? 0x04u : 0u);
}

#define CPU_ROTATE_REG(OP, SIZE)                                               \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.l %4,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP "." #SIZE " %%d0,%%d1\n\t"                                     \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d1,%0\n\t"                                               \
            "move.l %%d2,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(count), "d"(value), "d"(initial_ccr)                         \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_rotate_reg(uint32_t op, uint32_t bits, uint32_t count,
                           uint32_t value, uint32_t initial_x,
                           uint32_t *result, uint32_t *ccr)
{
    uint32_t initial_ccr = initial_x ? 0x10u : 0u;

    if (op == 0u) {
        switch (bits) {
        case 8u:
            CPU_ROTATE_REG(rol, b);
            break;
        case 16u:
            CPU_ROTATE_REG(rol, w);
            break;
        default:
            CPU_ROTATE_REG(rol, l);
            break;
        }
    } else if (op == 1u) {
        switch (bits) {
        case 8u:
            CPU_ROTATE_REG(ror, b);
            break;
        case 16u:
            CPU_ROTATE_REG(ror, w);
            break;
        default:
            CPU_ROTATE_REG(ror, l);
            break;
        }
    } else if (op == 2u) {
        switch (bits) {
        case 8u:
            CPU_ROTATE_REG(roxl, b);
            break;
        case 16u:
            CPU_ROTATE_REG(roxl, w);
            break;
        default:
            CPU_ROTATE_REG(roxl, l);
            break;
        }
    } else {
        switch (bits) {
        case 8u:
            CPU_ROTATE_REG(roxr, b);
            break;
        case 16u:
            CPU_ROTATE_REG(roxr, w);
            break;
        default:
            CPU_ROTATE_REG(roxr, l);
            break;
        }
    }
}

#undef CPU_ROTATE_REG

static void test_rotate_register_differential(void)
{
    static const uint32_t sizes[] = {8u, 16u, 32u};
    static const uint32_t counts[] = {1u, 2u, 3u, 7u, 8u, 15u, 16u, 31u};
    static const uint32_t values[] = {
        0x00000000u, 0x00000001u, 0x0000007fu, 0x00000080u,
        0x000000ffu, 0x00007fffu, 0x00008000u, 0x0000ffffu,
        0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u,
        0x89abcdefu,
    };

    for (uint32_t op = 0; op < 4u; ++op) {
        for (uint32_t s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); ++s) {
            uint32_t bits = sizes[s];

            for (uint32_t xi = 0; xi < 2u; ++xi) {
                for (uint32_t ci = 0; ci < (sizeof(counts) / sizeof(counts[0])); ++ci) {
                    uint32_t count = counts[ci];

                    for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
                        uint32_t id = 0x001a0000u + (op << 16) + (s << 13) +
                                      (xi << 12) + (ci << 7) + (vi << 3);
                        uint32_t got_result;
                        uint32_t got_ccr;
                        uint32_t exp_result;
                        uint32_t exp_ccr;

#ifdef CORETEST_SIM_TRACE_ROTATE
                        uart_puts("RT ");
                        uart_hex32(id);
                        uart_putc('\n');
#endif
                        cpu_rotate_reg(op, bits, count, values[vi], xi,
                                       &got_result, &got_ccr);
                        exp_ccr = rotate_reg_ref(op, bits, values[vi], count, xi,
                                                 &exp_result);
                        chk32(id + 0x00u, got_result, exp_result);
                        chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                    }
                }
            }
        }
    }

    mark(0x001df000u, 0x70780000u);
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

    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x131u) = 0xffu;
    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x141u) = 0x01u;
    __asm__ volatile(
        "lea 0x01ffa832,%%a0\n\t"
        "lea 0x01ffa842,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "addx.b -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x00096090u, got0, XMEM_TEST_BASE + 0x131u);
    chk32(0x00096094u, got1, XMEM_TEST_BASE + 0x141u);
    chk8(0x00096098u, rd8(XMEM_TEST_BASE + 0x141u), 0x00u);
    chk32(0x0009609cu, got2 & 0x1fu, 0x11u);

    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x151u) = 0x01u;
    *(volatile uint8_t *)(XMEM_TEST_BASE + 0x161u) = 0x00u;
    __asm__ volatile(
        "lea 0x01ffa852,%%a0\n\t"
        "lea 0x01ffa862,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "subx.b -(%%a0),-(%%a1)\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1\n\t"
        "move.l %%d0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk32(0x000960a0u, got0, XMEM_TEST_BASE + 0x151u);
    chk32(0x000960a4u, got1, XMEM_TEST_BASE + 0x161u);
    chk8(0x000960a8u, rd8(XMEM_TEST_BASE + 0x161u), 0xffu);
    chk32(0x000960acu, got2 & 0x1fu, 0x19u);
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

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x0080u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "trap #0\n"
        "1:\n\t"
        "move.l %%usp,%%a1\n\t"
        "move.l %%a1,%0\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x000a0090u, 0x0080u, 0x0000u, 0x2000u, 0x0000u);
    chk32(0x000a00a8u, got, RETURN_TEST_BASE + 0xe0u);

    wr32(FC_PROBE_ARM, FC_PROBE_DATA_PROG);
    wr32(DATA_FC_TEST_BASE + 0x00u, 0x1234abcdu);
    wr32(DATA_FC_TEST_BASE + 0x04u, 0xfeedfaceu);
    chk32(0x00160040u, rd32(DATA_FC_TEST_BASE + 0x04u), 0xfeedfaceu);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    wr32(DATA_FC_TEST_BASE + 0x0cu, 0x2468ace0u);
    arm_exception_recovery(0x0080u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.l 0x01ffaf0c,%%d0\n\t"
        "move.l %%d0,0x01ffaf08\n\t"
        "trap #0\n"
        "1:\n\t"
        "move.l 0x01ffaf08,%0\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x00160048u, 0x0080u, 0x0000u, 0x2000u, 0x0000u);
    chk32(0x00160060u, got, 0x2468ace0u);

    wr32(PROG_FC_TEST_BASE + 0x00u, 0x4e714e75u);
    __asm__ volatile(
        "jsr 0x01ffaf20\n\t"
        "moveq #0x6e,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk32(0x00160064u, got, 0x6eu);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    wr32(PROG_FC_TEST_BASE + 0x10u, 0x4e400000u);
    arm_exception_recovery(0x0080u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "jsr 0x01ffaf30\n\t"
        "move.l #0xbadbad,%%d0\n"
        "1:\n\t"
        "moveq #0x6f,%%d0\n\t"
        "move.l %%d0,%0\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr"
        : "=&d"(got)
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x00160068u, 0x0080u, 0x0000u, 0x2000u, 0x0000u);
    chk32(0x00160080u, got, 0x6fu);
    wr32(FC_PROBE_ARM, 0u);

    arm_exception_recovery(0x0020u);
    __asm__ volatile(
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l #0xbadbad,%%d0\n"
        "1:"
        :
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk_exception_frame(0x00130000u, 0x0020u, 0x0000u, 0x2700u, 0x0000u);

    arm_exception_recovery(0x0020u);
    __asm__ volatile(
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #0xbadbad,%%d0\n"
        "1:"
        :
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk_exception_frame(0x00130020u, 0x0020u, 0x0000u, 0x2700u, 0x0000u);

    arm_exception_recovery(0x0020u);
    __asm__ volatile(
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "movec %%vbr,%%d0\n\t"
        "move.l #0xbadbad,%%d0\n"
        "1:"
        :
        :
        : "a0", "a1", "d0", "cc", "memory");
    chk_exception_frame(0x00130040u, 0x0020u, 0x0000u, 0x2700u, 0x0000u);

    arm_exception_recovery(0x0020u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 0x01ff9800,%%a2\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "moves.l (%%a2),%%d0\n\t"
        "move.l #0xbadbad,%%d0\n"
        "1:"
        :
        :
        : "a0", "a1", "a2", "d0", "d2", "cc", "memory");
    chk_exception_frame(0x00130060u, 0x0020u, 0x0000u, 0x2700u, 0x0000u);
}

#define PMMU_REG_SCRATCH 0x01ffc010u

static void pmmu_write_tt0(uint32_t value)
{
    wr32(PMMU_REG_SCRATCH, value);
    __asm__ volatile("pmove 0x01ffc010,%%tt0" : : : "memory");
}

static uint32_t pmmu_read_tt0(void)
{
    __asm__ volatile("pmove %%tt0,0x01ffc010" : : : "memory");
    return rd32(PMMU_REG_SCRATCH);
}

static void pmmu_write_tt1(uint32_t value)
{
    wr32(PMMU_REG_SCRATCH, value);
    __asm__ volatile("pmove 0x01ffc010,%%tt1" : : : "memory");
}

static uint32_t pmmu_read_tt1(void)
{
    __asm__ volatile("pmove %%tt1,0x01ffc010" : : : "memory");
    return rd32(PMMU_REG_SCRATCH);
}

static void pmmu_write_tc(uint32_t value)
{
    wr32(PMMU_REG_SCRATCH, value);
    __asm__ volatile("pmove 0x01ffc010,%%tc" : : : "memory");
}

static uint32_t pmmu_read_tc(void)
{
    __asm__ volatile("pmove %%tc,0x01ffc010" : : : "memory");
    return rd32(PMMU_REG_SCRATCH);
}

static void pmmu_load_crp(void)
{
    __asm__ volatile("pmove 0x01ffc000,%%crp" : : : "memory");
}

static void pmmu_store_crp(void)
{
    __asm__ volatile("pmove %%crp,0x01ffc008" : : : "memory");
}

static void test_pmmu_register_directed(void)
{
    const uint32_t root = 0x01ffb000u;
    const uint32_t l2_ff = 0x01ffb400u;
    const uint32_t l2_01 = 0x01ffb800u;
    const uint32_t l3_test = 0x01ffbc00u;
    const uint32_t l2_00 = 0x01ffc400u;
    uint32_t translated_read;

    pmmu_write_tt0(0x12340777u);
    chk32(0x002a0000u, pmmu_read_tt0(), 0x12340777u);
    pmmu_write_tt0(0u);
    chk32(0x002a0004u, pmmu_read_tt0(), 0u);

    pmmu_write_tt1(0xabcd0555u);
    chk32(0x002a0008u, pmmu_read_tt1(), 0xabcd0555u);
    pmmu_write_tt1(0u);
    chk32(0x002a000cu, pmmu_read_tt1(), 0u);

    pmmu_write_tc(0u);
    chk32(0x002a0010u, pmmu_read_tc(), 0u);
    __asm__ volatile("pflusha" : : : "memory");

    for (uint32_t i = 0; i < 256u; ++i) {
        wr32(root + i * 4u, 0u);
        wr32(l2_ff + i * 4u, 0u);
        wr32(l2_01 + i * 4u, 0u);
        wr32(l3_test + i * 4u, 0u);
        wr32(l2_00 + i * 4u, 0u);
    }

    /* Short-format, used table pointers. Root limit admits all 256 entries. */
    wr32(root + 0xffu * 4u, l2_ff | 0x0au);
    wr32(root + 0x01u * 4u, l2_01 | 0x0au);
    wr32(root + 0x00u * 4u, l2_00 | 0x0au);

    /* Early-terminated identity regions keep ROM execution and RAM/stack live. */
    for (uint32_t i = 0xe0u; i <= 0xe3u; ++i) {
        wr32(l2_ff + i * 4u, 0xff000019u | (i << 16));
    }
    wr32(l2_ff + 0xf0u * 4u, 0xfff00019u);
    for (uint32_t i = 0u; i <= 3u; ++i) {
        wr32(l2_00 + i * 4u, 0x00000019u | (i << 16));
    }
    wr32(l2_01 + 0xffu * 4u, 0x01ff0019u);

    /* One fully walked page proves that logical and physical addresses differ. */
    wr32(l2_01 + 0x00u * 4u, l3_test | 0x0au);
    wr32(l3_test + 0x20u * 4u, 0x01ff9019u);
    wr32(l3_test + 0x30u * 4u, 0x01ffa01du);
    wr32(0x01ff9000u, 0x4d4d5530u);
    wr32(0x01ffa000u, 0x50524f54u);
    progress_char('t');

    wr32(0x01ffc000u, 0x00ff0002u);
    wr32(0x01ffc004u, root);
    pmmu_load_crp();
    pmmu_store_crp();
    chk32(0x002a0014u, rd32(0x01ffc008u), 0x00ff0002u);
    chk32(0x002a0018u, rd32(0x01ffc00cu), root);
    progress_char('c');

    /* E=1, PS=8, IS=0, TIA/TIB/TIC=8: 8+8+8+8 = 32. */
    progress_char('e');
    pmmu_write_tc(0x80808880u);
    translated_read = rd32(0x01002000u);
    wr32(0x01002000u, 0x57414c4bu);
    pmmu_write_tc(0u);
    progress_char('x');

    chk32(0x002a001cu, translated_read, 0x4d4d5530u);
    chk32(0x002a0020u, rd32(0x01ff9000u), 0x57414c4bu);
    chk32(0x002a0024u, pmmu_read_tc(), 0u);

    /* An invalid root entry must raise a recoverable 68030 access fault. */
    wr32(EXC_ALT_VBR + 0x08u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery_skip_data_cycle(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284"
        :
        :
        : "a0", "d0", "memory");
    __asm__ volatile("pflusha" : : : "memory");
    pmmu_write_tc(0x80808880u);
    __asm__ volatile(
        "move.l 0x02000000,%%d1\n\t"
        "move.l #0x00badbad,%%d1\n"
        "1:"
        :
        :
        : "d1", "memory");
    pmmu_write_tc(0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "d0", "memory");
    chk_access_fault_frame(0x002a0030u, 0x0008u, 0x2000u, 0x2000u, 0u);
    progress_char('i');

    /* WP must fault before the translated write reaches physical memory. */
    arm_exception_recovery_skip_data_cycle(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284"
        :
        :
        : "a0", "d0", "memory");
    __asm__ volatile("pflusha" : : : "memory");
    pmmu_write_tc(0x80808880u);
    __asm__ volatile(
        "move.l #0x57504641,0x01003000\n\t"
        "move.l #0x00badbad,%%d1\n"
        "1:"
        :
        :
        : "d1", "memory");
    pmmu_write_tc(0u);
    __asm__ volatile(
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "d0", "memory");
    chk_access_fault_frame(0x002a0050u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk32(0x002a0068u, rd32(0x01ffa000u), 0x50524f54u);
    progress_char('w');

    mark(0x002af000u, 0x706d6d75u);
}
static void test_moves_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;

    progress_char('a');
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

    progress_char('A');
    progress_char('b');
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

    progress_char('B');
    progress_char('c');
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

    progress_char('C');
    progress_char('d');
    wr32(MOVES_EXT_TEST_BASE + 0x00u, 0xe55a9abcu);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ffac00,%%a0\n\t"
        "move.l #0x12345678,%%d1\n\t"
        "moves.b (%%a0)+,%%d1\n\t"
        "move.l %%d1,%0\n\t"
        "move.l %%a0,%1\n\t"
        "lea 0x01ffac02,%%a0\n\t"
        "move.l #0x87654321,%%d1\n\t"
        "moves.w (%%a0)+,%%d1\n\t"
        "move.l %%d1,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d1", "d2", "memory");
    chk32(0x000a0140u, got0, 0x123456e5u);
    chk32(0x000a0144u, got1, MOVES_EXT_TEST_BASE + 0x01u);
    chk32(0x000a0148u, got2, 0x87659abcu);
    chk32(0x000a014cu, got3, MOVES_EXT_TEST_BASE + 0x04u);

    progress_char('D');
    progress_char('e');
    wr32(MOVES_EXT_TEST_BASE + 0x40u, 0u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ffac44,%%a3\n\t"
        "moves.l %%a3,-(%%a3)\n\t"
        "move.l %%a3,%0"
        : "=&d"(got0)
        :
        : "a3", "d2", "memory");
    /* M68000 PRM 6-26: MC68030 stores decremented An for MOVES.x An,-(An). */
    chk32(0x000a0150u, rd32(MOVES_EXT_TEST_BASE + 0x40u), MOVES_EXT_TEST_BASE + 0x40u);
    chk32(0x000a0154u, got0, MOVES_EXT_TEST_BASE + 0x40u);

    progress_char('v');
    progress_char('f');
    wr32(MOVES_EXT_TEST_BASE + 0x50u, MOVES_EXT_TEST_BASE + 0x64u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ffac54,%%a3\n\t"
        "moves.l -(%%a3),%%a3\n\t"
        "move.l %%a3,%0"
        : "=&d"(got0)
        :
        : "a3", "d2", "memory");
    chk32(0x000a0160u, got0, MOVES_EXT_TEST_BASE + 0x64u);

    progress_char('F');
    progress_char('g');
    wr32(MOVES_TEST_BASE + 0x30u, 0x11223344u);
    wr32(MOVES_TEST_BASE + 0x34u, 0x55667788u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff9831,%%a0\n\t"
        "move.l #0xa1b2c3d4,%%d0\n\t"
        "moves.l %%d0,(%%a0)+\n\t"
        "move.l %%a0,%0\n\t"
        "lea 0x01ff9831,%%a0\n\t"
        "moveq #0,%%d1\n\t"
        "moves.l (%%a0)+,%%d1\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d2", "memory");
    chk32(0x000a0170u, rd32(MOVES_TEST_BASE + 0x30u), 0x11a1b2c3u);
    chk32(0x000a0174u, rd32(MOVES_TEST_BASE + 0x34u), 0xd4667788u);
    chk32(0x000a0178u, got0, MOVES_TEST_BASE + 0x35u);
    chk32(0x000a017cu, got1, 0xa1b2c3d4u);
    chk32(0x000a0180u, got2, MOVES_TEST_BASE + 0x35u);

    progress_char('G');
    progress_char('h');
    wr32(MOVES_TEST_BASE + 0x40u, 0x22334455u);
    __asm__ volatile(
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "lea 0x01ff9843,%%a0\n\t"
        "move.w #0x6e7f,%%d0\n\t"
        "moves.w %%d0,-(%%a0)\n\t"
        "move.l %%a0,%0\n\t"
        "lea 0x01ff9841,%%a1\n\t"
        "moveq #0,%%d1\n\t"
        "moves.w (%%a1),%%d1\n\t"
        "move.l %%d1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "d0", "d1", "d2", "memory");
    chk32(0x000a0190u, rd32(MOVES_TEST_BASE + 0x40u), 0x226e7f55u);
    chk32(0x000a0194u, got0, MOVES_TEST_BASE + 0x41u);
    chk32(0x000a0198u, got1, 0x00006e7fu);

    progress_char('H');
    wr32(FC_PROBE_ARM, FC_PROBE_MOVES);
    progress_char('i');
    wr32(MOVES_FC_TEST_BASE + 0x00u, 0x13579bdfu);
    __asm__ volatile(
        "moveq #2,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "moveq #1,%%d3\n\t"
        "movec %%d3,%%dfc\n\t"
        "lea 0x01ffad00,%%a0\n\t"
        "moveq #0,%%d0\n\t"
        "moves.l (%%a0),%%d0\n\t"
        "moves.l %%d0,4(%%a0)\n\t"
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "d0", "d2", "d3", "cc", "memory");
    chk32(0x00160000u, got0, 0x13579bdfu);
    chk32(0x00160004u, rd32(MOVES_FC_TEST_BASE + 0x04u), 0x13579bdfu);

    progress_char('I');
    progress_char('j');
    wr32(MOVES_FC_TEST_BASE + 0x00u, 0x2468ace0u);
    __asm__ volatile(
        "moveq #2,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "moveq #1,%%d3\n\t"
        "movec %%d3,%%dfc\n\t"
        "lea 0x01ffacf0,%%a0\n\t"
        "moveq #0x10,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "moves.l 0(%%a0,%%d4:l),%%d0\n\t"
        "movea.l #0x14,%%a1\n\t"
        "moves.l %%d0,0(%%a0,%%a1:l)\n\t"
        "moveq #1,%%d2\n\t"
        "movec %%d2,%%sfc\n\t"
        "movec %%d2,%%dfc\n\t"
        "move.l %%d0,%0"
        : "=&d"(got0)
        :
        : "a0", "a1", "d0", "d2", "d3", "d4", "cc", "memory");
    chk32(0x00160008u, got0, 0x2468ace0u);
    chk32(0x0016000cu, rd32(MOVES_FC_TEST_BASE + 0x04u), 0x2468ace0u);
    wr32(FC_PROBE_ARM, 0u);
    progress_char('J');
}

static void test_cmp2_chk2_directed(void)
{
    /* M68000 PRM Table 3-18: X is unchanged, Z/C are defined, N/V are undefined. */
    const uint32_t cmp2_ccr_mask = 0x15u;
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
    chk32(0x000a0200u, got0 & cmp2_ccr_mask, 0x04u);
    chk32(0x000a0204u, got1 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a0208u, got2 & cmp2_ccr_mask, 0x01u);

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
    chk32(0x000a0220u, got0 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a0224u, got1 & cmp2_ccr_mask, 0x01u);
    chk32(0x000a0228u, got2 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a022cu, got3 & cmp2_ccr_mask, 0x04u);

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
    chk32(0x000a0230u, got0 & cmp2_ccr_mask, 0x04u);
    chk32(0x000a0234u, got1 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a0238u, got2 & cmp2_ccr_mask, 0x01u);

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
    chk32(0x000a0250u, got0 & cmp2_ccr_mask, 0x04u);
    chk32(0x000a0254u, got1 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a0258u, got2 & cmp2_ccr_mask, 0x01u);

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
    chk32(0x000a0260u, got0 & cmp2_ccr_mask, 0x04u);
    chk32(0x000a0264u, got1 & cmp2_ccr_mask, 0x00u);
    chk32(0x000a0268u, got2 & cmp2_ccr_mask, 0x01u);

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
    chk_exception_pc(0x00120018u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x00120038u, rd32(EXC_RECOVERY_PC));
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
    chk_exception_pc(0x00120138u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x00120158u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x00120198u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x001201b8u, rd32(EXC_RECOVERY_PC));
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
    uint32_t got2;

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

    __asm__ volatile(
        "move.l %%sp,%%a2\n\t"
        "movec %%msp,%%d7\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff99e0\n\t"
        "move.l #0x11112222,0x01ff99e4\n\t"
        "move.l #0x33334444,0x01ff99e8\n\t"
        "move.l #0x01ff99e0,%%d0\n\t"
        "movec %%d0,%%msp\n\t"
        "move.w #0x3700,%%sr\n\t"
        "rtd #8\n"
        "1:\n\t"
        "move.l %%sp,%0\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%1\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l %%a2,%%sp\n\t"
        "movec %%d7,%%msp"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a2", "d0", "d7", "cc", "memory");
    chk32(0x000a0420u, got0, RETURN_TEST_BASE + 0xecu);
    chk32(0x000a0424u, got1 & 0x3700u, 0x3700u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x0080u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "move.l %%sp,%%a2\n\t"
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff99f0\n\t"
        "move.l #0x55556666,0x01ff99f4\n\t"
        "lea 0x01ff99f0,%%a0\n\t"
        "move.l %%a0,%%usp\n\t"
        "move.w #0x0000,%%sr\n\t"
        "rtd #4\n"
        "1:\n\t"
        "move.l %%sp,0x01ff99d8\n\t"
        "trap #0\n"
        "2:\n\t"
        "move.l 0x01ff99d8,%0\n\t"
        "move.l %%sp,%1\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "move.l %%a2,%%sp"
        : "=&d"(got0), "=&d"(got2)
        :
        : "a0", "a2", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x000a0440u, 0x0080u, 0x0000u, 0x2000u, 0x0000u);
    chk32(0x000a0458u, got0, RETURN_TEST_BASE + 0xf8u);
    chk32(0x000a045cu, got2 & 1u, 0u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    for (uint32_t off = 0; off < 0x90u; off += 4u) {
        wr32(RTE_USER_TEST_BASE + off, 0u);
    }
    arm_exception_recovery(0x0080u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "move.l %%sp,%%a2\n\t"
        "lea 0x01ffb280,%%a0\n\t"
        "move.l %%a0,%%usp\n\t"
        "lea 0x01ffb240,%%a0\n\t"
        "move.w #0x0015,(%%a0)\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,2(%%a0)\n\t"
        "move.w #0x0000,6(%%a0)\n\t"
        "lea 2f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.l %%a0,%%sp\n\t"
        "rte\n"
        "1:\n\t"
        "movem.l %%sp,0x01ffb200\n\t"
        "lea 0x5a5aa55a,%%a0\n\t"
        "pea 0(%%a0)\n\t"
        "movem.l %%sp,0x01ffb204\n\t"
        "lea 3f,%%a0\n\t"
        "movem.l %%a0,0x01ffb208\n\t"
        "trap #0\n"
        "3:\n\t"
        "move.l #0xbadbad,0x01ffb20c\n"
        "2:\n\t"
        "move.l %%sp,0x01ffb20c\n\t"
        "move.l %%usp,%%a0\n\t"
        "movem.l %%a0,0x01ffb210\n\t"
        "moveq #0,%%d1\n\t"
        "movec %%d1,%%vbr\n\t"
        "move.l %%a2,%%sp"
        :
        :
        : "a0", "a1", "a2", "d0", "d1", "cc", "memory");
    chk_exception_frame(0x000a0460u, 0x0080u, 0x0000u, 0x201fu, 0x0015u);
    chk_exception_pc(0x000a0478u, rd32(RTE_USER_TEST_BASE + 0x08u));
    chk32(0x000a047cu, rd32(RTE_USER_TEST_BASE + 0x00u), RTE_USER_TEST_BASE + 0x80u);
    chk32(0x000a0480u, rd32(RTE_USER_TEST_BASE + 0x04u), RTE_USER_TEST_BASE + 0x7cu);
    chk32(0x000a0484u, rd32(RTE_USER_TEST_BASE + 0x0cu), RTE_USER_TEST_BASE + 0x48u);
    chk32(0x000a0488u, rd32(RTE_USER_TEST_BASE + 0x7cu), 0x5a5aa55au);
    chk32(0x000a048cu, rd32(EXC_REC_BASE + 0x10u), RTE_USER_TEST_BASE + 0x40u);
    chk32(0x000a0490u, rd32(RTE_USER_TEST_BASE + 0x10u), RTE_USER_TEST_BASE + 0x7cu);
}

static uint32_t bcd_byte_to_dec(uint32_t v)
{
    return ((v >> 4) & 0x0fu) * 10u + (v & 0x0fu);
}

static uint32_t dec_to_bcd_byte(uint32_t v)
{
    return ((v / 10u) << 4) | (v % 10u);
}

static void abcd_ref(uint32_t src, uint32_t dst, uint32_t initial_ccr,
                     uint32_t *src_result, uint32_t *dst_result,
                     uint32_t *ccr)
{
    uint32_t total = bcd_byte_to_dec(src) + bcd_byte_to_dec(dst) +
                     ((initial_ccr & 0x10u) ? 1u : 0u);
    uint32_t carry = total >= 100u;
    uint32_t res_dec = total % 100u;
    uint32_t res = dec_to_bcd_byte(res_dec);
    uint32_t z = res_dec == 0u ? (initial_ccr & 0x04u) : 0u;

    *src_result = src;
    *dst_result = (dst & ~0xffu) | res;
    *ccr = (carry ? 0x11u : 0u) | z;
}

static void sbcd_ref(uint32_t src, uint32_t dst, uint32_t initial_ccr,
                     uint32_t *src_result, uint32_t *dst_result,
                     uint32_t *ccr)
{
    uint32_t sub = bcd_byte_to_dec(src) + ((initial_ccr & 0x10u) ? 1u : 0u);
    uint32_t dst_dec = bcd_byte_to_dec(dst);
    uint32_t borrow = sub > dst_dec;
    uint32_t res_dec = borrow ? (dst_dec + 100u - sub) : (dst_dec - sub);
    uint32_t res = dec_to_bcd_byte(res_dec);
    uint32_t z = res_dec == 0u ? (initial_ccr & 0x04u) : 0u;

    *src_result = src;
    *dst_result = (dst & ~0xffu) | res;
    *ccr = (borrow ? 0x11u : 0u) | z;
}

static void nbcd_ref(uint32_t value, uint32_t initial_ccr, uint32_t *result,
                     uint32_t *ccr)
{
    uint32_t sub = bcd_byte_to_dec(value) + ((initial_ccr & 0x10u) ? 1u : 0u);
    uint32_t carry = sub != 0u;
    uint32_t res_dec = sub == 0u ? 0u : (100u - sub);
    uint32_t res = dec_to_bcd_byte(res_dec);
    uint32_t z = res_dec == 0u ? (initial_ccr & 0x04u) : 0u;

    *result = (value & ~0xffu) | res;
    *ccr = (carry ? 0x11u : 0u) | z;
}

#define CPU_BCD_REG(OP)                                                        \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %3,%%d0\n\t"                                               \
            "move.l %4,%%d1\n\t"                                               \
            "move.l %5,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP " %%d0,%%d1\n\t"                                               \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2"                                                   \
            : "=m"(*src_result), "=m"(*dst_result), "=m"(*ccr)                 \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_abcd_reg(uint32_t src, uint32_t dst, uint32_t initial_ccr,
                         uint32_t *src_result, uint32_t *dst_result,
                         uint32_t *ccr)
{
    CPU_BCD_REG(abcd);
}

static void cpu_sbcd_reg(uint32_t src, uint32_t dst, uint32_t initial_ccr,
                         uint32_t *src_result, uint32_t *dst_result,
                         uint32_t *ccr)
{
    CPU_BCD_REG(sbcd);
}

#undef CPU_BCD_REG

static void cpu_nbcd_reg(uint32_t value, uint32_t initial_ccr,
                         uint32_t *result, uint32_t *ccr)
{
    __asm__ volatile(
        "move.l %2,%%d0\n\t"
        "move.l %3,%%d1\n\t"
        "move.w %%d1,%%ccr\n\t"
        "nbcd %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1"
        : "=m"(*result), "=m"(*ccr)
        : "d"(value), "d"(initial_ccr)
        : "d0", "d1", "cc", "memory");
}

static void test_bcd_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x04u, 0x10u, 0x14u};
    static const struct {
        uint8_t src;
        uint8_t dst;
    } pairs[] = {
        {0x00u, 0x00u}, {0x00u, 0x99u}, {0x99u, 0x00u},
        {0x99u, 0x99u}, {0x15u, 0x27u}, {0x15u, 0x42u},
        {0x01u, 0x00u}, {0x00u, 0x01u}, {0x09u, 0x01u},
        {0x50u, 0x50u}, {0x49u, 0x50u}, {0x90u, 0x09u},
    };
    static const uint8_t values[] = {
        0x00u, 0x01u, 0x09u, 0x10u, 0x45u, 0x50u, 0x99u,
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
            for (uint32_t i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); ++i) {
                uint32_t id = 0x00200000u + (op << 12) + (ci << 8) + (i << 4);
                uint32_t src = 0xa5000000u | pairs[i].src;
                uint32_t dst = 0x5a000000u | pairs[i].dst;
                uint32_t got_src;
                uint32_t got_dst;
                uint32_t got_ccr;
                uint32_t exp_src;
                uint32_t exp_dst;
                uint32_t exp_ccr;

                if (op == 0u) {
                    cpu_abcd_reg(src, dst, ccrs[ci], &got_src, &got_dst, &got_ccr);
                    abcd_ref(src, dst, ccrs[ci], &exp_src, &exp_dst, &exp_ccr);
                } else {
                    cpu_sbcd_reg(src, dst, ccrs[ci], &got_src, &got_dst, &got_ccr);
                    sbcd_ref(src, dst, ccrs[ci], &exp_src, &exp_dst, &exp_ccr);
                }

                chk32(id + 0x00u, got_src, exp_src);
                chk32(id + 0x04u, got_dst, exp_dst);
                chk32(id + 0x08u, got_ccr & 0x15u, exp_ccr);
            }
        }
    }

    for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
        for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
            uint32_t id = 0x00202000u + (ci << 8) + (vi << 4);
            uint32_t value = 0xc3000000u | values[vi];
            uint32_t got_result;
            uint32_t got_ccr;
            uint32_t exp_result;
            uint32_t exp_ccr;

            cpu_nbcd_reg(value, ccrs[ci], &got_result, &got_ccr);
            nbcd_ref(value, ccrs[ci], &exp_result, &exp_ccr);
            chk32(id + 0x00u, got_result, exp_result);
            chk32(id + 0x04u, got_ccr & 0x15u, exp_ccr);
        }
    }

    mark(0x0020f000u, 0xbcd00000u);
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

    wr32(BCD_TEST_BASE + 0x60u, 0x11452233u);
    wr32(BCD_TEST_BASE + 0x64u, 0xaa00bbccu);
    wr32(BCD_TEST_BASE + 0x68u, 0u);
    wr32(BCD_TEST_BASE + 0x6cu, 0u);
    __asm__ volatile(
        "lea 0x01ff9a60,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "nbcd 1(%%a0)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ff9a68\n\t"
        "lea 0x01ff9a64,%%a0\n\t"
        "move.w #0x04,%%ccr\n\t"
        "nbcd 1(%%a0)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ff9a6c"
        :
        :
        : "a0", "d1", "cc", "memory");
    chk32(0x000a0570u, rd32(BCD_TEST_BASE + 0x60u), 0x11552233u);
    chk32(0x000a0574u, rd32(BCD_TEST_BASE + 0x68u) & 0x15u, 0x11u);
    chk32(0x000a0578u, rd32(BCD_TEST_BASE + 0x64u), 0xaa00bbccu);
    chk32(0x000a057cu, rd32(BCD_TEST_BASE + 0x6cu) & 0x15u, 0x04u);
}

static void pack_reg_ref(uint32_t src, uint32_t dst, uint32_t adjust,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *dst_result, uint32_t *ccr)
{
    uint32_t adjusted = ((src & 0xffffu) + (adjust & 0xffffu)) & 0xffffu;
    uint32_t packed = ((adjusted >> 4) & 0xf0u) | (adjusted & 0x0fu);

    *src_result = src;
    *dst_result = (dst & ~0xffu) | packed;
    *ccr = initial_ccr;
}

static void unpk_reg_ref(uint32_t src, uint32_t dst, uint32_t adjust,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *dst_result, uint32_t *ccr)
{
    uint32_t unpacked = (((src >> 4) & 0x0fu) << 8) | (src & 0x0fu);
    uint32_t adjusted = (unpacked + (adjust & 0xffffu)) & 0xffffu;

    *src_result = src;
    *dst_result = (dst & ~0xffffu) | adjusted;
    *ccr = initial_ccr;
}

#define CPU_PACK_REG(OP, ADJ)                                                  \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %3,%%d0\n\t"                                               \
            "move.l %4,%%d1\n\t"                                               \
            "move.l %5,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP " %%d0,%%d1,#" #ADJ "\n\t"                                     \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2"                                                   \
            : "=m"(*src_result), "=m"(*dst_result), "=m"(*ccr)                 \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_pack_reg(uint32_t adjust_index, uint32_t src, uint32_t dst,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *dst_result, uint32_t *ccr)
{
    if (adjust_index == 0u) {
        CPU_PACK_REG(pack, 0);
    } else if (adjust_index == 1u) {
        CPU_PACK_REG(pack, 0x0101);
    } else {
        CPU_PACK_REG(pack, 0xffff);
    }
}

static void cpu_unpk_reg(uint32_t adjust_index, uint32_t src, uint32_t dst,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *dst_result, uint32_t *ccr)
{
    if (adjust_index == 0u) {
        CPU_PACK_REG(unpk, 0);
    } else if (adjust_index == 1u) {
        CPU_PACK_REG(unpk, 0x0101);
    } else {
        CPU_PACK_REG(unpk, 0xffff);
    }
}

#undef CPU_PACK_REG

static void test_pack_unpk_register_differential(void)
{
    static const uint32_t adjusts[] = {0x0000u, 0x0101u, 0xffffu};
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const uint32_t pack_srcs[] = {
        0x00000000u, 0x00000205u, 0x00000912u, 0x00009009u,
        0x11220205u, 0x99887725u, 0xffff9999u,
    };
    static const uint32_t unpk_srcs[] = {
        0x00000000u, 0x00000025u, 0x00000099u, 0x11223345u,
        0x99887725u, 0xffff00a3u,
    };
    static const uint32_t dsts[] = {0x00000000u, 0x12345678u, 0x89abcdefu};

    for (uint32_t op = 0; op < 2u; ++op) {
        uint32_t value_count = op == 0u ?
            (sizeof(pack_srcs) / sizeof(pack_srcs[0])) :
            (sizeof(unpk_srcs) / sizeof(unpk_srcs[0]));

        for (uint32_t ai = 0; ai < (sizeof(adjusts) / sizeof(adjusts[0])); ++ai) {
            for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
                for (uint32_t vi = 0; vi < value_count; ++vi) {
                    for (uint32_t di = 0; di < (sizeof(dsts) / sizeof(dsts[0])); ++di) {
                        uint32_t id = 0x00210000u + (op << 15) + (ai << 12) +
                                      (ci << 11) + (vi << 5) + (di << 3);
                        uint32_t src = op == 0u ? pack_srcs[vi] : unpk_srcs[vi];
                        uint32_t got_src;
                        uint32_t got_dst;
                        uint32_t got_ccr;
                        uint32_t exp_src;
                        uint32_t exp_dst;
                        uint32_t exp_ccr;

                        if (op == 0u) {
                            cpu_pack_reg(ai, src, dsts[di], ccrs[ci],
                                         &got_src, &got_dst, &got_ccr);
                            pack_reg_ref(src, dsts[di], adjusts[ai], ccrs[ci],
                                         &exp_src, &exp_dst, &exp_ccr);
                        } else {
                            cpu_unpk_reg(ai, src, dsts[di], ccrs[ci],
                                         &got_src, &got_dst, &got_ccr);
                            unpk_reg_ref(src, dsts[di], adjusts[ai], ccrs[ci],
                                         &exp_src, &exp_dst, &exp_ccr);
                        }

                        chk32(id + 0x00u, got_src, exp_src);
                        chk32(id + 0x04u, got_dst, exp_dst);
                        chk32(id + 0x08u, got_ccr & 0x1fu, exp_ccr);
                    }
                }
            }
        }
    }

    mark(0x0021f000u, 0x0acc0000u);
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

    wr32(PACK_TEST_BASE + 0xa0u, 0x11040722u);
    wr32(PACK_TEST_BASE + 0xb0u, 0u);
    __asm__ volatile(
        "lea 0x01ff9ba3,%%a0\n\t"
        "lea 0x01ff9bb1,%%a1\n\t"
        "pack -(%%a0),-(%%a1),#0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0660u, got0, PACK_TEST_BASE + 0xa1u);
    chk32(0x000a0664u, got1, PACK_TEST_BASE + 0xb0u);
    chk32(0x000a0668u, rd32(PACK_TEST_BASE + 0xa0u), 0x11040722u);
    chk32(0x000a066cu, rd32(PACK_TEST_BASE + 0xb0u), 0x47000000u);

    wr32(PACK_TEST_BASE + 0xc0u, 0x48000000u);
    wr32(PACK_TEST_BASE + 0xd0u, 0x11223344u);
    __asm__ volatile(
        "lea 0x01ff9bc1,%%a0\n\t"
        "lea 0x01ff9bd3,%%a1\n\t"
        "unpk -(%%a0),-(%%a1),#0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%a1,%1"
        : "=&d"(got0), "=&d"(got1)
        :
        : "a0", "a1", "cc", "memory");
    chk32(0x000a0670u, got0, PACK_TEST_BASE + 0xc0u);
    chk32(0x000a0674u, got1, PACK_TEST_BASE + 0xd1u);
    chk32(0x000a0678u, rd32(PACK_TEST_BASE + 0xc0u), 0x48000000u);
    chk32(0x000a067cu, rd32(PACK_TEST_BASE + 0xd0u), 0x11040844u);
}

#ifdef CORETEST_SIM_FOCUS_SDRAM_BERR
static void test_sdram_combined_write_berr(void)
{
    uint32_t ready = 0u;

    for (uint32_t i = 0; i < 1000000u; ++i) {
        ready = VESTA->SYS_STATUS & SYS_SDRAM_READY;
        if (ready != 0u) break;
    }
    chk32(0x00100600u, ready, SYS_SDRAM_READY);

    wr32(BERR_SDRAM_TARGET, 0x0badcafeu);
    chk32(0x00100604u, rd32(BERR_SDRAM_TARGET), 0x0badcafeu);

#ifdef CORETEST_SIM_FB_GUARD
    VEGA->MODE = VEGA_MODE_320x200;
    VEGA->FB_BASE = BERR_SDRAM_TARGET - 0x02000000u;
    VEGA->FB_PITCH = 320u;
    VEGA->FB_FORMAT = VEGA_FMT_INDEX8;
    VEGA->FB_VIEW = VEGA_FB_VIEW_(0u, 0u);
    VEGA->FB_VIRTUAL = VEGA_FB_VIRTUAL_(320u, 200u);
    VEGA->FB_WRAP = 0u;
    VEGA->SCENE_GENERATION = 1u;
    VEGA->CTRL = VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_FB_EN;
    VEGA->PRESENT_CTRL = VEGA_PRESENT_SUBMIT;

    ready = 0u;
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if (VEGA->PRESENT_COMPLETED_GENERATION == 1u) {
            ready = 1u;
            break;
        }
    }
    chk32(0x00100608u, ready, 1u);
    chk32(0x0010060cu,
          VEGA->STATUS & VEGA_STAT_CONFIG_ERROR, 0u);
#endif

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x08u, (uint32_t)(uintptr_t)_h_recover);
#ifdef CORETEST_SIM_FB_GUARD
    arm_exception_recovery_skip_data_cycle(0x0008u);
#else
    arm_exception_recovery(0x0008u);
#endif

    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x13579bdf,%%d1\n\t"
        BERR_SDRAM_ARM_ASM
        "move.l %%d1,0x02000100\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");

    chk_access_fault_frame(0x00100610u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100628u, 0x0105u);
    chk_access_fault_long(0x00100630u, 0x34u, 0x13579bdfu);
    chk_access_fault_long(0x0010062cu, 0x2cu, BERR_SDRAM_TARGET);
    chk32(0x00100634u, VESTA->BUS_FAULT_STATUS & 0x000007ffu,
          BUS_FAULT_VALID | BUS_FAULT_DEVICE | BUS_FAULT_WRITE |
          (5u << BUS_FAULT_FC_SHIFT));
    chk32(0x00100638u, VESTA->BUS_FAULT_ADDRESS, BERR_SDRAM_TARGET);
#ifdef CORETEST_SIM_FB_GUARD
    chk32(0x0010063cu, VESTA->BUS_FAULT_TARGET,
          BUS_FAULT_TARGET_FRAMEBUFFER_GUARD);
#else
    chk32(0x0010063cu, VESTA->BUS_FAULT_TARGET,
          BUS_FAULT_TARGET_EXTERNAL);
#endif
    chk32(0x00100640u,
          (VESTA->BUS_FAULT_CYCLES_LO != 0u ||
           VESTA->BUS_FAULT_CYCLES_HI != 0u) ? 1u : 0u, 1u);
    chk32(0x00100644u, VESTA->BUS_FAULT_LOST, 0u);
    chk32(0x00100648u, VESTA->BUS_TIMEOUT_CYCLES, 2048u);
    VESTA->BUS_FAULT_ACK = BUS_FAULT_VALID;
    chk32(0x0010064cu,
          VESTA->BUS_FAULT_STATUS & BUS_FAULT_VALID, 0u);
#ifdef CORETEST_SIM_FB_GUARD
    chk32(0x00100650u, rd32(BERR_SDRAM_TARGET), 0x0badcafeu);
#else
    chk32(0x00100650u, rd32(BERR_SDRAM_TARGET), 0x13579bdfu);
#endif

    arm_exception_recovery_skip_data_cycle(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x2468ace0,%%d1\n\t"
        "move.l %%d1,0xfff00900\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");

    chk_access_fault_frame(0x00100660u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100678u, 0x0105u);
    chk_access_fault_long(0x0010067cu, 0x34u, 0x2468ace0u);
    chk_access_fault_long(0x00100680u, 0x2cu, BERR_UNMAPPED_TARGET);
    chk32(0x00100684u, VESTA->BUS_FAULT_STATUS & 0x000007ffu,
          BUS_FAULT_VALID | BUS_FAULT_UNMAPPED | BUS_FAULT_WRITE |
          (2u << BUS_FAULT_SIZE_SHIFT) |
          (5u << BUS_FAULT_FC_SHIFT));
    chk32(0x00100688u, VESTA->BUS_FAULT_ADDRESS,
          BERR_UNMAPPED_TARGET);
    chk32(0x0010068cu, VESTA->BUS_FAULT_TARGET,
          BUS_FAULT_TARGET_UNMAPPED);
    chk32(0x00100690u, VESTA->BUS_FAULT_LOST, 1u);
    VESTA->BUS_FAULT_ACK = BUS_FAULT_VALID;
    chk32(0x00100694u,
          VESTA->BUS_FAULT_STATUS & BUS_FAULT_VALID, 0u);
}
#endif

static void test_exception_recovery_directed(void)
{
    arm_exception_recovery(0x000cu);
    __asm__ volatile(
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "lea 2f,%%a0\n\t"
        "addq.l #1,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "jmp (%%a0)\n"
        "2:\n\t"
        "nop\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_access_fault_frame(0x00100700u, 0x000cu, 0x2000u, 0x2000u, 1u);
    /* MC68030 UM 8.2: address errors use pipeline rerun state, not data fault address. */
    chk32(0x00100718u, rd32(EXC_REC_BASE + 0x24u) & 0xf000u, 0x3000u);
    chk32(0x0010071cu, rd32(EXC_STAGE_B_ADDR) - 2u, rd32(EXC_EXPECTED_ADDR));

#ifdef CORETEST_SIM_IRQ
    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x08u, (uint32_t)(uintptr_t)_h_recover);
    wr32(BERR_SIM_TARGET, 0x5a5aa55au);
    arm_exception_recovery(0x0008u);
    progress_char('k');
#ifdef CORETEST_SIM_PROGRESS
    __asm__ volatile(
        "move.l %%sp,0x01ff92b0\n\t"
        "move.l %%a6,0x01ff92b4"
        :
        :
        : "memory");
#endif
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #" BERR_ARM_SUP_DATA_READ ",0xfff00604\n\t"
        "move.l 0xfff00608,%%d1\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
#ifdef CORETEST_SIM_PROGRESS
    __asm__ volatile(
        "move.l %%sp,0x01ff92b8\n\t"
        "move.l %%a6,0x01ff92bc\n\t"
        "move.l #0x6a,0xfff00500"
        :
        :
        : "memory");
#endif
    progress_char('K');
    chk_access_fault_frame(0x00100720u, 0x0008u, 0x2000u, 0x2000u, 0u);
    progress_char('l');
    chk_access_fault_status(0x00100738u, 0x0145u);
    progress_char('L');
    /* Format A/B frame offset $08 is internal processor state and is not scored. */
    chk_access_fault_long(0x001007a8u, 0x2cu, BERR_SIM_TARGET);
    progress_char('m');

    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x13579bdf,%%d1\n\t"
        "move.l #" BERR_ARM_SUP_DATA_WRITE ",0xfff00604\n\t"
        "move.l %%d1,0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100740u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100758u, 0x0105u);
    chk_access_fault_long(0x001007b8u, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x4e754e71u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #" BERR_ARM_SUP_PROG_READ ",0xfff00604\n\t"
        "move.l #0xfff00608,%%a1\n\t"
        "jsr (%%a1)\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "move.l 0x01ff9270,%%d2\n\t"
        "beq 2f\n\t"
        "addq.l #4,%%sp\n"
        "2:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "d2", "cc", "memory");
    chk_access_fault_frame(0x00100760u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk32(0x00100778u, rd32(EXC_REC_BASE + 0x24u) & 0xf100u, 0x5000u);
    chk32(0x001007c4u, rd32(EXC_STAGE_B_ADDR), BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0xa55a5aa5u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #" BERR_ARM_SUP_DATA_READ ",0xfff00604\n\t"
        "move.b 0xfff00608,%%d1\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100b00u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100b18u, 0x0155u);
    chk_access_fault_long(0x00100b1cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x55aaa55au);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #" BERR_ARM_SUP_DATA_READ ",0xfff00604\n\t"
        "move.w 0xfff00608,%%d1\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100b20u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100b38u, 0x0165u);
    chk_access_fault_long(0x00100b3cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x01020304u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x13579bdf,%%d1\n\t"
        "move.l #" BERR_ARM_SUP_DATA_WRITE ",0xfff00604\n\t"
        "move.b %%d1,0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100b40u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100b58u, 0x0115u);
    chk_access_fault_long(0x00100b5cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x11223344u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x2468ace0,%%d1\n\t"
        "move.l #" BERR_ARM_SUP_DATA_WRITE ",0xfff00604\n\t"
        "move.w %%d1,0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100b60u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100b78u, 0x0125u);
    chk_access_fault_long(0x00100b7cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x00000000u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #" BERR_ARM_SUP_RMC_DATA_READ ",0xfff00604\n\t"
        "tas.b 0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100b80u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100b98u, 0x01d5u);
    chk_access_fault_long(0x00100b9cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x01020304u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x01020304,%%d1\n\t"
        "move.l #0x11223344,%%d2\n\t"
        "move.l #" BERR_ARM_SUP_RMC_DATA_READ ",0xfff00604\n\t"
        "cas.l %%d1,%%d2,0xfff00608\n\t"
        "move.l #0xbadbad,%%d2\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    chk_access_fault_frame(0x00100ba0u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100bb8u, 0x01c5u);
    chk_access_fault_long(0x00100bbcu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x5aa55aa5u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.l #" BERR_ARM_USER_DATA_READ ",0xfff00604\n\t"
        "move.l 0xfff00608,%%d1\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100bc0u, 0x0008u, 0x2000u, 0x0000u, 0u);
    chk_access_fault_status(0x00100bd8u, 0x0141u);
    chk_access_fault_long(0x00100bdcu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x01020304u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #0x11223344,%%d1\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.l #" BERR_ARM_USER_DATA_WRITE ",0xfff00604\n\t"
        "move.l %%d1,0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100be0u, 0x0008u, 0x2000u, 0x0000u, 0u);
    chk_access_fault_status(0x00100bf8u, 0x0101u);
    chk_access_fault_long(0x00100bfcu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x4e754e71u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 0x01ff99e0,%%a1\n\t"
        "move.l %%a1,%%usp\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x0000,%%sr\n\t"
        "move.l #" BERR_ARM_USER_PROG_READ ",0xfff00604\n\t"
        "jsr 0xfff00608\n\t"
        "move.l #0xbadbad,%%d1\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "cc", "memory");
    chk_access_fault_frame(0x00100c00u, 0x0008u, 0x2000u, 0x0000u, 0u);
    chk32(0x00100c18u, rd32(EXC_REC_BASE + 0x24u) & 0xf100u, 0x5000u);
    chk32(0x00100c1cu, rd32(EXC_STAGE_B_ADDR), BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x11111111u);
    wr32(BERR_SIM_TARGET + 4u, 0x22222222u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d4\n\t"
        "movec %%d4,%%vbr\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "lea 0xfff00608,%%a0\n\t"
        "lea 0xfff0060c,%%a1\n\t"
        "move.l #0x11111111,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x22222222,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.l #" BERR_ARM_SUP_RMC_DATA_READ ",0xfff00604\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.l #0xbadbad,%%d3\n"
        "1:\n\t"
        "moveq #0,%%d4\n\t"
        "movec %%d4,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk_access_fault_frame(0x00100c20u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100c38u, 0x01c5u);
    chk_access_fault_long(0x00100c3cu, 0x2cu, BERR_SIM_TARGET);

    wr32(BERR_SIM_TARGET, 0x33333333u);
    wr32(BERR_SIM_TARGET + 4u, 0x44444444u);
    arm_exception_recovery(0x0008u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d4\n\t"
        "movec %%d4,%%vbr\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "lea 0xfff0060c,%%a0\n\t"
        "lea 0xfff00608,%%a1\n\t"
        "move.l #0x44444444,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x33333333,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.l #" BERR_ARM_SUP_RMC_DATA_READ ",0xfff00604\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.l #0xbadbad,%%d3\n"
        "1:\n\t"
        "moveq #0,%%d4\n\t"
        "movec %%d4,%%vbr"
        :
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    chk_access_fault_frame(0x00100c40u, 0x0008u, 0x2000u, 0x2000u, 0u);
    chk_access_fault_status(0x00100c58u, 0x01c5u);
    chk_access_fault_long(0x00100c5cu, 0x2cu, BERR_SIM_TARGET);
#endif

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x10u, (uint32_t)(uintptr_t)_h_recover);
    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0x4afc\n"
        "1:\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "memory");
    chk_exception_frame(0x00100000u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100800u, rd32(EXC_EXPECTED_ADDR));

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0x4afc\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100020u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100804u, rd32(EXC_EXPECTED_ADDR));

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0x4848\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100030u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100808u, rd32(EXC_EXPECTED_ADDR));

    {
        uint32_t cacr_readback;

        __asm__ volatile(
            "moveq #0,%%d0\n\t"
            ".word 0x4e7b,0x0002\n\t"   /* movec d0,cacr */
            "moveq #1,%%d0\n\t"
            ".word 0x4e7a,0x0002\n\t"   /* movec cacr,d0 */
            "move.l %%d0,%0"
            : "=d"(cacr_readback)
            :
            : "d0", "memory");
        chk32(0x00100640u, cacr_readback, 0u);
        chk32(0x00100660u, 1u, 1u);
    }

    {
        uint32_t divu_w_pc;

        arm_exception_recovery(0x0014u);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.l #1234,%%d0\n"
            "2:\n\t"
            "divu.w #0,%%d0\n"
            "1:"
            : "=&d"(divu_w_pc)
            :
            : "a0", "d0", "cc", "memory");
        chk_exception_frame(0x00100040u, 0x0014u, 0x2000u, 0x2000u, 0x2000u);
        chk_exception_pc(0x001007d0u, rd32(EXC_RECOVERY_PC));
        chk32(0x00100a00u, rd32(EXC_REC_BASE + 0x24u), divu_w_pc);
    }

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
    chk_exception_pc(0x001007e0u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x001007e4u, rd32(EXC_RECOVERY_PC));

    {
        uint32_t chk_w_pc;

        *(volatile uint16_t *)(SCRATCH_BASE + 0x1a0u) = 4u;
        arm_exception_recovery(0x0018u);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "moveq #5,%%d0\n"
            "2:\n\t"
            "chk.w 0x01ff92a0,%%d0\n"
            "1:"
            : "=&d"(chk_w_pc)
            :
            : "a0", "d0", "cc", "memory");
        chk_exception_frame(0x00100060u, 0x0018u, 0x2000u, 0x2000u, 0x2000u);
        chk_exception_pc(0x00100814u, rd32(EXC_RECOVERY_PC));
        chk32(0x00100a04u, rd32(EXC_REC_BASE + 0x24u), chk_w_pc);
    }

    {
        uint32_t trapv_pc;

        arm_exception_recovery(0x001cu);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.w #0x02,%%ccr\n"
            "2:\n\t"
            "trapv\n"
            "1:"
            : "=&d"(trapv_pc)
            :
            : "a0", "cc", "memory");
        chk_exception_frame(0x00100080u, 0x001cu, 0x2000u, 0x201fu, 0x2002u);
        chk_exception_pc(0x001007d4u, rd32(EXC_RECOVERY_PC));
        chk32(0x00100a08u, rd32(EXC_REC_BASE + 0x24u), trapv_pc);
    }

    {
        uint32_t trapeq_pc;

        arm_exception_recovery(0x001cu);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "moveq #0,%%d0\n\t"
            "move.w #0,%%ccr\n\t"
            "cmp.l %%d0,%%d0\n"
            "2:\n\t"
            "trapeq\n"
            "1:"
            : "=&d"(trapeq_pc)
            :
            : "a0", "d0", "cc", "memory");
        chk_exception_frame(0x00100140u, 0x001cu, 0x2000u, 0x201fu, 0x2004u);
        chk_exception_pc(0x001007d8u, rd32(EXC_RECOVERY_PC));
        chk32(0x00100a0cu, rd32(EXC_REC_BASE + 0x24u), trapeq_pc);
    }

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
    chk_exception_pc(0x001007e8u, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x001007ecu, rd32(EXC_RECOVERY_PC));

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
    chk_exception_pc(0x001007dcu, rd32(EXC_RECOVERY_PC));

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

    {
        uint32_t trace_instr_pc;
        uint32_t trace_next_pc;

        arm_exception_recovery(0x0024u);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,%1\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.w #0xa700,%%sr\n\t"
            "2:\n\t"
            "nop\n"
            "1:"
            : "=&d"(trace_instr_pc), "=&d"(trace_next_pc)
            :
            : "a0", "cc", "memory");
        chk_exception_frame(0x001000c0u, 0x0024u, 0x2000u, 0xe700u, 0xa700u);
        chk_exception_pc(0x001007f0u, trace_next_pc);
        chk32(0x001007f4u, rd32(EXC_REC_BASE + 0x24u), trace_instr_pc);
    }

    {
        uint32_t trace_branch_pc;
        uint32_t trace_target_pc;

        arm_exception_recovery(0x0024u);
        __asm__ volatile(
            "lea 3f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%1\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.w #0x6700,%%sr\n\t"
            "nop\n"
            "2:\n\t"
            "bra.s 3f\n\t"
            "nop\n"
            "3:\n\t"
            "nop\n"
            "1:"
            : "=&d"(trace_target_pc), "=&d"(trace_branch_pc)
            :
            : "a0", "cc", "memory");
        chk_exception_frame(0x00100900u, 0x0024u, 0x2000u, 0xe700u, 0x6700u);
        chk_exception_pc(0x00100920u, trace_target_pc);
        chk32(0x00100924u, rd32(EXC_REC_BASE + 0x24u), trace_branch_pc);
    }

    {
        uint32_t trace_jmp_pc;
        uint32_t trace_target_pc;

        arm_exception_recovery(0x0024u);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 3f,%%a1\n\t"
            "move.l %%a1,%1\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.w #0x6700,%%sr\n\t"
            "nop\n"
            "2:\n\t"
            "jmp (%%a1)\n"
            "3:\n\t"
            "move.w #0x2700,%%sr\n\t"
            "bra.w 1f\n"
            "1:"
            : "=&d"(trace_jmp_pc), "=&d"(trace_target_pc)
            :
            : "a0", "a1", "cc", "memory");
        chk_exception_frame(0x00100940u, 0x0024u, 0x2000u, 0xe700u, 0x6700u);
        chk_exception_pc(0x00100960u, trace_target_pc);
        chk32(0x00100964u, rd32(EXC_REC_BASE + 0x24u), trace_jmp_pc);
    }

    {
        uint32_t trace_rts_pc;
        uint32_t trace_return_pc;

        arm_exception_recovery(0x0024u);
        __asm__ volatile(
            "lea 4f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%1\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "jsr 3f\n"
            "2:\n\t"
            "move.w #0x2700,%%sr\n\t"
            "bra.w 5f\n"
            "3:\n\t"
            "move.w #0x6700,%%sr\n"
            "4:\n\t"
            "rts\n"
            "5:"
            : "=&d"(trace_rts_pc), "=&d"(trace_return_pc)
            :
            : "a0", "cc", "memory");
        chk_exception_frame(0x00100980u, 0x0024u, 0x2000u, 0xe700u, 0x6700u);
        chk_exception_pc(0x001009a0u, trace_return_pc);
        chk32(0x001009a4u, rd32(EXC_REC_BASE + 0x24u), trace_rts_pc);
    }

    {
        uint32_t trace_sr_pc;
        uint32_t trace_next_pc;

        arm_exception_recovery(0x0024u);
        __asm__ volatile(
            "lea 2f,%%a0\n\t"
            "move.l %%a0,%0\n\t"
            "lea 1f,%%a0\n\t"
            "move.l %%a0,%1\n\t"
            "move.l %%a0,0x01ff9284\n\t"
            "move.w #0x6700,%%sr\n\t"
            "nop\n"
            "2:\n\t"
            "ori.w #0x0001,%%sr\n"
            "1:"
            : "=&d"(trace_sr_pc), "=&d"(trace_next_pc)
            :
            : "a0", "cc", "memory");
        chk_exception_frame(0x001009c0u, 0x0024u, 0x2000u, 0xe71fu, 0x6701u);
        chk_exception_pc(0x001009e0u, trace_next_pc);
        chk32(0x001009e4u, rd32(EXC_REC_BASE + 0x24u), trace_sr_pc);
    }

    arm_exception_recovery(0x0028u);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0xa000\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x001000e0u, 0x0028u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x0010080cu, rd32(EXC_EXPECTED_ADDR));

    arm_exception_recovery(0x002cu);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0xf200\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100100u, 0x002cu, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100810u, rd32(EXC_EXPECTED_ADDR));

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "2:\n\t"
        ".word 0xefc8\n\t" /* BFINS with invalid address-register direct EA. */
        ".word 0x0001\n"
        "1:"
        :
        :
        : "a0", "memory");
    chk_exception_frame(0x00100320u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100338u, rd32(EXC_EXPECTED_ADDR));

    arm_exception_recovery(0x0010u);
    __asm__ volatile(
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff92a0\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "lea 0x01ff9100,%%a1\n\t"
        "2:\n\t"
        ".word 0xeed9\n\t" /* BFSET with invalid postincrement EA. */
        ".word 0x0001\n"
        "1:"
        :
        :
        : "a0", "a1", "memory");
    chk_exception_frame(0x00100340u, 0x0010u, 0x0000u, 0x2000u, 0x2000u);
    chk_exception_pc(0x00100358u, rd32(EXC_EXPECTED_ADDR));

#define EXPECT_ILLEGAL_BF_OP(ID, OPWORD) \
    do { \
        arm_exception_recovery(0x0010u); \
        __asm__ volatile( \
            "lea 2f,%%a0\n\t" \
            "move.l %%a0,0x01ff92a0\n\t" \
            "lea 1f,%%a0\n\t" \
            "move.l %%a0,0x01ff9284\n\t" \
            "2:\n\t" \
            ".word " #OPWORD "\n\t" \
            ".word 0x0001\n" \
            "1:" \
            : \
            : \
            : "a0", "memory"); \
        chk_exception_frame((ID), 0x0010u, 0x0000u, 0x2000u, 0x2000u); \
        chk_exception_pc((ID) + 0x18u, rd32(EXC_EXPECTED_ADDR)); \
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
static void test_vesta_timer_vectored(void)
{
    for (uint32_t off = 0; off < 0x400u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + (80u * 4u),
         (uint32_t)(uintptr_t)_h_vesta_timer);

    arm_exception_recovery(80u * 4u);
    VESTA->IRQ_ENABLE = 0u;
    VESTA->IRQ_SOFT = 0u;
    VESTA->IRQ_ACK = 0xffffffffu;
    VESTA->TIMER[0].CTRL = 0u;
    VESTA->TIMER[0].STATUS = TMR_EXPIRED;
    VESTA->TIMER[0].LOAD = 128u;
    VESTA->IRQ_CFG[IRQ_SRC_TIMER0] =
        IRQ_CFG_LEVEL(4) | IRQ_CFG_VECTOR(80);
    VESTA->IRQ_ENABLE = IRQ_BIT(IRQ_SRC_TIMER0);

    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l #5,0xfff00408\n\t"
        "stop #0x2000\n\t"
        "move.w #0x2700,%%sr\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "d0", "cc", "memory");

    chk_exception_frame(0x0010f000u, 80u * 4u, 0u,
                        0x2700u, 0x2000u);
    chk32(0x0010f018u, VESTA->IRQ_PENDING & IRQ_BIT(IRQ_SRC_TIMER0), 0u);
    chk32(0x0010f01cu, VESTA->TIMER[0].STATUS, 0u);
    chk32(0x0010f020u, VESTA->TIMER[0].CTRL & TMR_ENABLE, 0u);
    chk32(0x0010f024u, VESTA->IRQ_ENABLE, 0u);
}

static void test_interrupt_autovector_directed(void)
{
    uint32_t got;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t loop_count;
    uint32_t loop_reg;
    uint32_t stacked_pc;
    uint32_t irq_loop_count;
    uint32_t irq_loop_reg;

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
        "move.l #3,0xfff00600\n\t"
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
        "move.l #0,0xfff00600"
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
        "move.l #3,0xfff00600\n\t"
        "stop #0x2000\n"
        "1:\n\t"
        "move.w #0x2700,%%sr\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l #0,0xfff00600"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110020u, 0x006cu, 0x0000u, 0x2700u, 0x2000u);

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
        "move.l #3,0xfff00600\n\t"
        "move.w #0x2300,%%sr\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l %%d1,%0"
        : "=&d"(got)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x00110040u, rd32(EXC_REC_BASE + 0x00u), 0u);
    chk32(0x00110044u, got & 0x2700u, 0x2300u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x74u, (uint32_t)(uintptr_t)_h_recover);

    arm_exception_recovery(0x0074u);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #5,0xfff00600\n\t"
        "move.w #0x2300,%%sr\n\t"
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
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110060u, 0x0074u, 0x0000u, 0x2700u, 0x2300u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x7cu, (uint32_t)(uintptr_t)_h_recover);

    arm_exception_recovery(0x007cu);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #7,0xfff00600\n\t"
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
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110080u, 0x007cu, 0x0000u, 0x2700u, 0x2700u);

    /* MC68030 UM 8.1.1: dropping below level 7 rearms the next NMI edge. */
    arm_exception_recovery(0x007cu);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #7,0xfff00600\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr"
        :
        :
        : "a0", "d0", "cc", "memory");
    chk_exception_frame(0x00110200u, 0x007cu, 0x0000u, 0x2700u, 0x2700u);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x6cu, (uint32_t)(uintptr_t)_h_irq_return);

    arm_exception_recovery(0x006cu);
    wr32(EXC_REC_BASE + 0x1cu, 0u);
    wr32(EXC_REC_BASE + 0x20u, 0u);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "lea 1f,%%a0\n\t"
        "move.l %%a0,%0\n\t"
        "lea 3f,%%a0\n\t"
        "move.l %%a0,%1\n\t"
        "moveq #0,%%d3\n\t"
        "move.w #0x03ff,%%d2\n\t"
        "move.w #0x2000,%%sr\n"
        "1:\n\t"
        "addq.l #1,%%d3\n\t"
        "cmp.l #4,%%d3\n\t"
        "bne 2f\n\t"
        "move.l #3,0xfff00600\n"
        "2:\n\t"
        "dbra %%d2,1b\n"
        "3:\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l %%d3,%2\n\t"
        "moveq #0,%%d0\n\t"
        "move.w %%d2,%%d0\n\t"
        "move.l %%d0,%3"
        : "=&d"(loop_start), "=&d"(loop_end), "=&d"(loop_count), "=&d"(loop_reg)
        :
        : "a0", "d0", "d2", "d3", "cc", "memory");
    chk_exception_frame(0x001100a0u, 0x006cu, 0x0000u, 0x2700u, 0x2000u);
    chk32(0x001100b8u, loop_count, 0x400u);
    chk32(0x001100bcu, loop_reg & 0xffffu, 0xffffu);
    irq_loop_count = rd32(EXC_REC_BASE + 0x1cu);
    mark(0x001100c0u, irq_loop_count);
    if (irq_loop_count < 4u) stop_fail(0x001100c0u, irq_loop_count, 4u);
    if (irq_loop_count >= 0x400u) stop_fail(0x001100c4u, irq_loop_count, 0x3ffu);
    irq_loop_reg = rd32(EXC_REC_BASE + 0x20u);
    mark(0x001100c8u, irq_loop_reg);
    if (irq_loop_reg >= 0x400u) stop_fail(0x001100c8u, irq_loop_reg, 0x3ffu);
    if (irq_loop_reg == 0xffffu) stop_fail(0x001100cau, irq_loop_reg, 0x03ffu);
    stacked_pc = rd32(EXC_REC_BASE + 0x08u);
    mark(0x001100ccu, stacked_pc);
    if (stacked_pc < loop_start) stop_fail(0x001100ccu, stacked_pc, loop_start);
    if (stacked_pc > loop_end) stop_fail(0x001100d0u, stacked_pc, loop_end);

    for (uint32_t off = 0; off < 0x100u; off += 4u) {
        wr32(EXC_ALT_VBR + off, (uint32_t)(uintptr_t)_h_default);
    }
    wr32(EXC_ALT_VBR + 0x6cu, (uint32_t)(uintptr_t)_h_irq_return);
    wr32(EXC_ALT_VBR + 0x80u, (uint32_t)(uintptr_t)_h_recover);
    for (uint32_t off = 0; off < 0x90u; off += 4u) {
        wr32(IRQ_USER_TEST_BASE + off, 0u);
    }
    arm_exception_recovery(0x0080u);
    wr32(IRQ_SIM_REQ, 0u);
    __asm__ volatile(
        "move.l #0x01ff9500,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l %%sp,%%a2\n\t"
        "movem.l %%a2,0x01ffb300\n\t"
        "lea 0x01ffb380,%%a0\n\t"
        "move.l %%a0,%%usp\n\t"
        "lea 2f,%%a0\n\t"
        "move.l %%a0,0x01ff9284\n\t"
        "move.l #3,0xfff00600\n\t"
        "move.w #0x0000,%%sr\n"
        "1:\n\t"
        "tst.l 0x01ff9270\n\t"
        "beq 1b\n\t"
        "movem.l %%sp,0x01ffb304\n\t"
        "move.l 0x01ff9270,0x01ffb320\n\t"
        "move.l 0x01ff9274,0x01ffb324\n\t"
        "move.l 0x01ff9278,0x01ffb328\n\t"
        "move.l 0x01ff927c,0x01ffb32c\n\t"
        "move.l 0x01ff9280,0x01ffb330\n\t"
        "trap #0\n"
        "2:\n\t"
        "move.l %%usp,%%a0\n\t"
        "movem.l %%a0,0x01ffb308\n\t"
        "move.w #0x2700,%%sr\n\t"
        "move.l #0,0xfff00600\n\t"
        "moveq #0,%%d0\n\t"
        "movec %%d0,%%vbr\n\t"
        "move.l %%a2,%%sp"
        :
        :
        : "a0", "a2", "d0", "cc", "memory");
    chk32(0x001100e0u, rd32(EXC_REC_BASE + 0x00u), 2u);
    chk32(0x001100e4u, rd32(EXC_REC_BASE + 0x0cu) & 0x0fffu, 0x0080u);
    chk32(0x001100e8u, rd32(EXC_REC_BASE + 0x0cu) & 0xf000u, 0u);
    chk32(0x001100ecu, rd32(EXC_REC_BASE + 0x04u) & 0x2700u, 0u);
    chk32(0x001100f0u, rd32(EXC_REC_BASE + 0x10u) & 1u, 0u);
    chk32(0x001100f4u, rd32(EXC_REC_BASE + 0x08u) & 1u, 0u);
    chk32(0x001100f8u, rd32(IRQ_USER_TEST_BASE + 0x20u), 1u);
    chk32(0x001100fcu, rd32(IRQ_USER_TEST_BASE + 0x2cu) & 0x0fffu, 0x006cu);
    chk32(0x00110100u, rd32(IRQ_USER_TEST_BASE + 0x2cu) & 0xf000u, 0u);
    chk32(0x00110104u, rd32(IRQ_USER_TEST_BASE + 0x24u) & 0x2700u, 0u);
    chk32(0x00110108u, rd32(IRQ_USER_TEST_BASE + 0x30u), rd32(IRQ_USER_TEST_BASE) - 8u);
    chk32(0x0011010cu, rd32(IRQ_USER_TEST_BASE + 0x04u), IRQ_USER_TEST_BASE + 0x80u);
    chk32(0x00110110u, rd32(IRQ_USER_TEST_BASE + 0x08u), IRQ_USER_TEST_BASE + 0x80u);
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

    wr32(SHIFT_TEST_BASE + 0xb0u, 0x11224001u);
    wr32(SHIFT_TEST_BASE + 0xb4u, 0x00011234u);
    wr32(SHIFT_TEST_BASE + 0xb8u, 0x80017788u);
    wr32(SHIFT_TEST_BASE + 0xbcu, 0x00015566u);
    for (uint32_t off = 0xd0u; off <= 0xdcu; off += 4u) {
        wr32(SHIFT_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #2,%%d4\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "lsl.w 0x01ffa0b0(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa0d0\n\t"
        "moveq #-1,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "asr.w 0x01ffa0b5(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa0d4\n\t"
        "movea.l #8,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "rol.w 0x01ffa0b0(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa0d8\n\t"
        "moveq #3,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "roxr.w 0x01ffa0b0(%%d4:l:4)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa0dc"
        :
        :
        : "a1", "d1", "d4", "cc", "memory");
    chk32(0x000b0210u, rd32(SHIFT_TEST_BASE + 0xb0u), 0x11228002u);
    chk32(0x000b0214u, rd32(SHIFT_TEST_BASE + 0xb4u), 0x00001234u);
    chk32(0x000b0218u, rd32(SHIFT_TEST_BASE + 0xb8u), 0x00037788u);
    chk32(0x000b021cu, rd32(SHIFT_TEST_BASE + 0xbcu), 0x80005566u);
    chk32(0x000b0220u, rd32(SHIFT_TEST_BASE + 0xd0u) & 0x1fu, 0x08u);
    chk32(0x000b0224u, rd32(SHIFT_TEST_BASE + 0xd4u) & 0x1fu, 0x15u);
    chk32(0x000b0228u, rd32(SHIFT_TEST_BASE + 0xd8u) & 0x1fu, 0x01u);
    chk32(0x000b022cu, rd32(SHIFT_TEST_BASE + 0xdcu) & 0x1fu, 0x19u);

    for (uint32_t off = 0x20u; off < 0xa8u; off += 4u) {
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
        "move.l %%d1,0x01ffa054\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x10,%%ccr\n\t"
        "roxl.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa058\n\t"
        "move.l %%d1,0x01ffa05c\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x10,%%ccr\n\t"
        "roxr.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa060\n\t"
        "move.l %%d1,0x01ffa064\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "asl.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa068\n\t"
        "move.l %%d1,0x01ffa06c\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "asr.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa070\n\t"
        "move.l %%d1,0x01ffa074\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "lsl.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa078\n\t"
        "move.l %%d1,0x01ffa07c\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "lsr.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa080\n\t"
        "move.l %%d1,0x01ffa084\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "rol.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa088\n\t"
        "move.l %%d1,0x01ffa08c\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x1f,%%ccr\n\t"
        "ror.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa090\n\t"
        "move.l %%d1,0x01ffa094\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x01,%%ccr\n\t"
        "roxl.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa098\n\t"
        "move.l %%d1,0x01ffa09c\n\t"
        "move.l #0x80000001,%%d0\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x01,%%ccr\n\t"
        "roxr.l %%d2,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ffa0a0\n\t"
        "move.l %%d1,0x01ffa0a4"
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
    chk32(0x000b01b8u, rd32(SHIFT_TEST_BASE + 0x58u), 0x80000001u);
    chk32(0x000b01bcu, rd32(SHIFT_TEST_BASE + 0x5cu) & 0x1fu, 0x19u);
    chk32(0x000b01c0u, rd32(SHIFT_TEST_BASE + 0x60u), 0x80000001u);
    chk32(0x000b01c4u, rd32(SHIFT_TEST_BASE + 0x64u) & 0x1fu, 0x19u);
    chk32(0x000b01c8u, rd32(SHIFT_TEST_BASE + 0x68u), 0x80000001u);
    chk32(0x000b01ccu, rd32(SHIFT_TEST_BASE + 0x6cu) & 0x1fu, 0x18u);
    chk32(0x000b01d0u, rd32(SHIFT_TEST_BASE + 0x70u), 0x80000001u);
    chk32(0x000b01d4u, rd32(SHIFT_TEST_BASE + 0x74u) & 0x1fu, 0x18u);
    chk32(0x000b01d8u, rd32(SHIFT_TEST_BASE + 0x78u), 0x80000001u);
    chk32(0x000b01dcu, rd32(SHIFT_TEST_BASE + 0x7cu) & 0x1fu, 0x18u);
    chk32(0x000b01e0u, rd32(SHIFT_TEST_BASE + 0x80u), 0x80000001u);
    chk32(0x000b01e4u, rd32(SHIFT_TEST_BASE + 0x84u) & 0x1fu, 0x18u);
    chk32(0x000b01e8u, rd32(SHIFT_TEST_BASE + 0x88u), 0x80000001u);
    chk32(0x000b01ecu, rd32(SHIFT_TEST_BASE + 0x8cu) & 0x1fu, 0x18u);
    chk32(0x000b01f0u, rd32(SHIFT_TEST_BASE + 0x90u), 0x80000001u);
    chk32(0x000b01f4u, rd32(SHIFT_TEST_BASE + 0x94u) & 0x1fu, 0x18u);
    chk32(0x000b01f8u, rd32(SHIFT_TEST_BASE + 0x98u), 0x80000001u);
    chk32(0x000b01fcu, rd32(SHIFT_TEST_BASE + 0x9cu) & 0x1fu, 0x08u);
    chk32(0x000b0200u, rd32(SHIFT_TEST_BASE + 0xa0u), 0x80000001u);
    chk32(0x000b0204u, rd32(SHIFT_TEST_BASE + 0xa4u) & 0x1fu, 0x08u);

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

static uint32_t condition_true_ref(uint32_t cond, uint32_t ccr)
{
    uint32_t n = (ccr >> 3) & 1u;
    uint32_t z = (ccr >> 2) & 1u;
    uint32_t v = (ccr >> 1) & 1u;
    uint32_t c = ccr & 1u;

    switch (cond & 0xfu) {
    case 0x0u: return 1u;
    case 0x1u: return 0u;
    case 0x2u: return !z && !c;
    case 0x3u: return z || c;
    case 0x4u: return !c;
    case 0x5u: return c;
    case 0x6u: return !z;
    case 0x7u: return z;
    case 0x8u: return !v;
    case 0x9u: return v;
    case 0xau: return !n;
    case 0xbu: return n;
    case 0xcu: return n == v;
    case 0xdu: return n != v;
    case 0xeu: return !z && (n == v);
    default: return z || (n != v);
    }
}

static void store_scc_matrix(uint32_t addr, uint32_t ccr)
{
    __asm__ volatile(
        "move.l %0,%%a0\n\t"
        "move.w %1,%%ccr\n\t"
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
        : "d"(addr), "d"(ccr & 0x1fu)
        : "a0", "cc", "memory");
}

static void store_bcc_mask(uint32_t addr, uint32_t ccr)
{
    __asm__ volatile(
        "move.l %0,%%a0\n\t"
        "move.l %1,%%d1\n\t"
        "moveq #0,%%d0\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bhi 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000004,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bls 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000008,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bcc 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000010,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bcs 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000020,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bne 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000040,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "beq 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000080,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bvc 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000100,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bvs 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000200,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bpl 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000400,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bmi 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000800,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bge 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00001000,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "blt 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00002000,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "bgt 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00004000,%%d0\n"
        "2:\n\t"
        "move.w %%d1,%%ccr\n\t"
        "ble 1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00008000,%%d0\n"
        "2:\n\t"
        "move.l %%d0,(%%a0)"
        :
        : "d"(addr), "d"(ccr & 0x1fu)
        : "a0", "d0", "d1", "cc", "memory");
}

static void store_dbcc_mask(uint32_t addr, uint32_t ccr)
{
    __asm__ volatile(
        "move.l %0,%%a0\n\t"
        "move.l %1,%%d1\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbt %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000001,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbf %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000002,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbhi %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000004,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbls %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000008,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbcc %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000010,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbcs %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000020,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbne %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000040,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbeq %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000080,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbvc %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000100,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbvs %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000200,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbpl %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000400,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbmi %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000800,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbge %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00001000,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dblt %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00002000,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dbgt %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00004000,%%d0\n"
        "2:\n\t"
        "moveq #1,%%d2\n\t"
        "move.w %%d1,%%ccr\n\t"
        "dble %%d2,1f\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00008000,%%d0\n"
        "2:\n\t"
        "move.l %%d0,(%%a0)"
        :
        : "d"(addr), "d"(ccr & 0x1fu)
        : "a0", "d0", "d1", "d2", "cc", "memory");
}

static void store_trapcc_mask(uint32_t ccr)
{
    __asm__ volatile(
        "move.l %0,%%d1\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapt\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000001,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapf\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000002,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "traphi\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000004,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapls\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000008,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapcc\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000010,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapcs\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000020,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapne\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000040,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapeq\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000080,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapvc\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000100,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapvs\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000200,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trappl\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000400,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapmi\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00000800,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapge\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00001000,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "traplt\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00002000,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "trapgt\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00004000,0x01ffa218\n"
        "2:\n\t"
        "lea 1f,%%a1\n\t"
        "move.l %%a1,0x01ff9284\n\t"
        "move.w %%d1,%%ccr\n\t"
        "traple\n\t"
        "bra 2f\n"
        "1:\n\t"
        "ori.l #0x00008000,0x01ffa218\n"
        "2:"
        :
        : "d"(ccr & 0x1fu)
        : "a0", "a1", "d0", "d1", "cc", "memory");
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

    for (uint32_t off = 0x80u; off < 0x90u; off += 4u) {
        wr32(COND_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l #0x12345678,%%d0\n\t"
        "move.l #0x89abcdef,%%d1\n\t"
        "move.l #0x0badc0de,%%d2\n\t"
        "move.l #0x55667788,%%d3\n\t"
        "move.w #0,%%ccr\n\t"
        "st %%d0\n\t"
        "sf %%d1\n\t"
        "seq %%d2\n\t"
        "cmp.l %%d3,%%d3\n\t"
        "seq %%d3\n\t"
        "move.l %%d0,0x01ffa180\n\t"
        "move.l %%d1,0x01ffa184\n\t"
        "move.l %%d2,0x01ffa188\n\t"
        "move.l %%d3,0x01ffa18c"
        :
        :
        : "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000c0140u, rd32(COND_TEST_BASE + 0x80u), 0x123456ffu);
    chk32(0x000c0144u, rd32(COND_TEST_BASE + 0x84u), 0x89abcd00u);
    chk32(0x000c0148u, rd32(COND_TEST_BASE + 0x88u), 0x0badc000u);
    chk32(0x000c014cu, rd32(COND_TEST_BASE + 0x8cu), 0x556677ffu);

    for (uint32_t off = 0x90u; off <= 0xa0u; off += 4u) {
        wr32(COND_TEST_BASE + off, 0x55555555u);
    }
    __asm__ volatile(
        "moveq #1,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "cmp.l %%d0,%%d0\n\t"
        "seq 0x01ffa190(%%d4:l)\n\t"
        "moveq #2,%%d4\n\t"
        "moveq #1,%%d0\n\t"
        "cmpi.l #0,%%d0\n\t"
        "seq 0x01ffa190(%%d4:l)\n\t"
        "moveq #3,%%d4\n\t"
        "moveq #1,%%d0\n\t"
        "cmpi.l #0,%%d0\n\t"
        "sne 0x01ffa190(%%d4:l)\n\t"
        "move.l #0x7fff0004,%%d4\n\t"
        "st 0x01ffa190(%%d4:w)\n\t"
        "move.l #0x7fff0005,%%d4\n\t"
        "sf 0x01ffa190(%%d4:w)\n\t"
        "moveq #2,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "spl 0x01ffa194(%%d4:l:2)\n\t"
        "movea.l #3,%%a1\n\t"
        "move.w #0x08,%%ccr\n\t"
        "smi 0x01ffa198(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa1a0"
        :
        :
        : "a1", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000c0150u, rd32(COND_TEST_BASE + 0x90u), 0x55ff00ffu);
    chk32(0x000c0154u, rd32(COND_TEST_BASE + 0x94u), 0xff005555u);
    chk32(0x000c0158u, rd32(COND_TEST_BASE + 0x98u), 0xff5555ffu);
    chk32(0x000c015cu, rd32(COND_TEST_BASE + 0x9cu), 0x55555555u);
    chk32(0x000c0160u, rd32(COND_TEST_BASE + 0xa0u) & 0x271fu, 0x2708u);

    for (uint32_t ccr = 0; ccr < 0x20u; ++ccr) {
        uint32_t row = COND_TEST_BASE + 0x100u;

        for (uint32_t off = 0; off < 0x10u; off += 4u) {
            wr32(row + off, 0xaaaaaaaau);
        }

        store_scc_matrix(row, ccr);

        for (uint32_t cond = 0; cond < 0x10u; ++cond) {
            uint32_t exp = condition_true_ref(cond, ccr) ? 0xffu : 0x00u;
            chk8(0x000c0400u + (ccr << 5) + cond, rd8(row + cond), exp);
        }
    }

    for (uint32_t ccr = 0; ccr < 0x20u; ++ccr) {
        uint32_t exp = 0u;

        wr32(COND_TEST_BASE + 0x110u, 0xaaaaaaaau);
        store_bcc_mask(COND_TEST_BASE + 0x110u, ccr);

        for (uint32_t cond = 2; cond < 0x10u; ++cond) {
            if (condition_true_ref(cond, ccr)) {
                exp |= 1u << cond;
            }
        }

        chk32(0x000c0800u + (ccr << 2), rd32(COND_TEST_BASE + 0x110u), exp);
    }

    for (uint32_t ccr = 0; ccr < 0x20u; ++ccr) {
        uint32_t exp = 0u;

        wr32(COND_TEST_BASE + 0x114u, 0xaaaaaaaau);
        store_dbcc_mask(COND_TEST_BASE + 0x114u, ccr);

        for (uint32_t cond = 0; cond < 0x10u; ++cond) {
            if (!condition_true_ref(cond, ccr)) {
                exp |= 1u << cond;
            }
        }

        chk32(0x000c0c00u + (ccr << 2), rd32(COND_TEST_BASE + 0x114u), exp);
    }

    static const uint8_t trapcc_ccrs[] = {0x00u, 0x0fu, 0x02u, 0x10u};
    for (uint32_t row = 0; row < 4u; ++row) {
        uint32_t ccr = trapcc_ccrs[row];
        uint32_t exp = 0u;
        uint32_t trap_count = 0u;

        wr32(COND_TEST_BASE + 0x118u, 0u);
        arm_exception_recovery(0x001cu);
        store_trapcc_mask(ccr);

        for (uint32_t cond = 0; cond < 0x10u; ++cond) {
            if (condition_true_ref(cond, ccr)) {
                exp |= 1u << cond;
                ++trap_count;
            }
        }

        chk32(0x000c1000u + (row << 3), rd32(COND_TEST_BASE + 0x118u), exp);
        chk32(0x000c1004u + (row << 3), rd32(EXC_REC_BASE + 0x00u), trap_count);
    }
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

    __asm__ volatile(
        "moveq #3,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "sub.l %%d1,%%d1\n\t"
        "dbeq %%d0,1f\n\t"
        "move.l %%d0,0x01ffa164\n\t"
        "bra 2f\n"
        "1:\n\t"
        "move.l #0x11111111,0x01ffa164\n"
        "2:\n\t"
        "moveq #3,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "subq.l #1,%%d1\n\t"
        "dbne %%d0,3f\n\t"
        "move.l #0x22222222,0x01ffa168\n\t"
        "bra 4f\n"
        "3:\n\t"
        "move.l %%d0,0x01ffa168\n"
        "4:\n\t"
        "moveq #3,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "subq.l #1,%%d1\n\t"
        "dbmi %%d0,5f\n\t"
        "move.l %%d0,0x01ffa16c\n\t"
        "bra 6f\n"
        "5:\n\t"
        "move.l #0x33333333,0x01ffa16c\n"
        "6:"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000c0234u, rd32(COND_TEST_BASE + 0x64u), 3u);
    chk32(0x000c0238u, rd32(COND_TEST_BASE + 0x68u), 2u);
    chk32(0x000c023cu, rd32(COND_TEST_BASE + 0x6cu), 3u);
}

static void bit_reg_ref(uint32_t op, uint32_t count, uint32_t value,
                        uint32_t initial_ccr, uint32_t *result,
                        uint32_t *ccr)
{
    uint32_t mask = 1u << (count & 31u);
    uint32_t bit_was_set = (value & mask) != 0u;
    uint32_t res = value;

    if (op == 0u) {
        res |= mask;
    } else if (op == 1u) {
        res &= ~mask;
    } else if (op == 2u) {
        res ^= mask;
    }

    *result = res;
    *ccr = (initial_ccr & ~0x04u) | (bit_was_set ? 0u : 0x04u);
}

#define CPU_BIT_REG(OP)                                                        \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.l %4,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP " %%d0,%%d1\n\t"                                               \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d1,%0\n\t"                                               \
            "move.l %%d2,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(count), "d"(value), "d"(initial_ccr)                         \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_bit_reg(uint32_t op, uint32_t count, uint32_t value,
                        uint32_t initial_ccr, uint32_t *result,
                        uint32_t *ccr)
{
    if (op == 0u) {
        CPU_BIT_REG(bset);
    } else if (op == 1u) {
        CPU_BIT_REG(bclr);
    } else if (op == 2u) {
        CPU_BIT_REG(bchg);
    } else {
        CPU_BIT_REG(btst);
    }
}

#undef CPU_BIT_REG

static void test_bitops_register_differential(void)
{
    static const uint32_t counts[] = {
        0u, 1u, 7u, 8u, 15u, 16u, 31u, 32u, 33u, 63u,
    };
    static const uint32_t values[] = {
        0x00000000u, 0x00000001u, 0x80000000u, 0xffffffffu,
        0x55555555u, 0xaaaaaaaau, 0x12345678u, 0x89abcdefu,
    };
    static const uint32_t ccrs[] = {0x00u, 0x1fu};

    for (uint32_t op = 0; op < 4u; ++op) {
        for (uint32_t ci = 0; ci < (sizeof(counts) / sizeof(counts[0])); ++ci) {
            for (uint32_t vi = 0; vi < (sizeof(values) / sizeof(values[0])); ++vi) {
                for (uint32_t fi = 0; fi < (sizeof(ccrs) / sizeof(ccrs[0])); ++fi) {
                    uint32_t id = 0x001f0000u + (op << 13) + (ci << 8) +
                                  (vi << 4) + (fi << 3);
                    uint32_t got_result;
                    uint32_t got_ccr;
                    uint32_t exp_result;
                    uint32_t exp_ccr;

                    cpu_bit_reg(op, counts[ci], values[vi], ccrs[fi],
                                &got_result, &got_ccr);
                    bit_reg_ref(op, counts[ci], values[vi], ccrs[fi],
                                &exp_result, &exp_ccr);
                    chk32(id + 0x00u, got_result, exp_result);
                    chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                }
            }
        }
    }

    mark(0x001ff000u, 0xb1700000u);
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

    wr32(BITOP_TEST_BASE + 0x80u, 0x10203040u);
    wr32(BITOP_TEST_BASE + 0x84u, 0u);
    wr32(BITOP_TEST_BASE + 0x88u, 0xaabbccddu);
    for (uint32_t off = 0x90u; off <= 0xa0u; off += 4u) {
        wr32(BITOP_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #4,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "bset #7,0x01ffa380(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa390\n\t"
        "moveq #13,%%d0\n\t"
        "moveq #5,%%d4\n\t"
        "bchg %%d0,0x01ffa380(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa394\n\t"
        "moveq #7,%%d0\n\t"
        "moveq #4,%%d4\n\t"
        "btst %%d0,0x01ffa380(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa398\n\t"
        "moveq #5,%%d4\n\t"
        "bclr #5,0x01ffa380(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa39c\n\t"
        "moveq #-1,%%d4\n\t"
        "bset #0,0x01ffa387(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffa3a0"
        :
        :
        : "d0", "d1", "d4", "cc", "memory");
    chk32(0x000c0330u, rd32(BITOP_TEST_BASE + 0x80u), 0x10203040u);
    chk32(0x000c0334u, rd32(BITOP_TEST_BASE + 0x84u), 0x80000100u);
    chk32(0x000c0338u, rd32(BITOP_TEST_BASE + 0x88u), 0xaabbccddu);
    chk32(0x000c033cu, rd32(BITOP_TEST_BASE + 0x90u) & 0x04u, 0x04u);
    chk32(0x000c0340u, rd32(BITOP_TEST_BASE + 0x94u) & 0x04u, 0x04u);
    chk32(0x000c0344u, rd32(BITOP_TEST_BASE + 0x98u) & 0x04u, 0x00u);
    chk32(0x000c0348u, rd32(BITOP_TEST_BASE + 0x9cu) & 0x04u, 0x00u);
    chk32(0x000c034cu, rd32(BITOP_TEST_BASE + 0xa0u) & 0x04u, 0x04u);
}

static uint32_t mul_word_ccr_ref(uint32_t op, uint32_t src, uint32_t dst,
                                 uint32_t initial_ccr, uint32_t *result)
{
    uint32_t product;

    if (op == 0u) {
        product = (src & 0xffffu) * (dst & 0xffffu);
    } else {
        int32_t srcs = (int16_t)(src & 0xffffu);
        int32_t dsts = (int16_t)(dst & 0xffffu);

        product = (uint32_t)(srcs * dsts);
    }

    *result = product;
    return (initial_ccr & 0x10u) |
           ((product & 0x80000000u) ? 0x08u : 0u) |
           (product == 0u ? 0x04u : 0u);
}

#define CPU_MUL_WORD_REG(OP)                                                   \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %2,%%d0\n\t"                                               \
            "move.l %3,%%d1\n\t"                                               \
            "move.l %4,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP ".w %%d0,%%d1\n\t"                                             \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d1,%0\n\t"                                               \
            "move.l %%d2,%1"                                                   \
            : "=m"(*result), "=m"(*ccr)                                        \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_mul_word_reg(uint32_t op, uint32_t src, uint32_t dst,
                             uint32_t initial_ccr, uint32_t *result,
                             uint32_t *ccr)
{
    if (op == 0u) {
        CPU_MUL_WORD_REG(mulu);
    } else {
        CPU_MUL_WORD_REG(muls);
    }
}

#undef CPU_MUL_WORD_REG

static void test_mul_word_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const uint32_t srcs[] = {
        0x00000000u, 0x00000001u, 0x00000002u, 0x0000007fu,
        0x00000080u, 0x000000ffu, 0x00000100u, 0x00007fffu,
        0x00008000u, 0x0000fffeu, 0x0000ffffu, 0x12345678u,
        0x89abcdefu,
    };
    static const uint32_t dsts[] = {
        0x00000000u, 0x00000001u, 0x00000002u, 0x00007fffu,
        0x00008000u, 0x0000fffeu, 0x0000ffffu, 0x12345678u,
        0x89abcdefu,
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
            for (uint32_t si = 0; si < (sizeof(srcs) / sizeof(srcs[0])); ++si) {
                for (uint32_t di = 0; di < (sizeof(dsts) / sizeof(dsts[0])); ++di) {
                    uint32_t id = 0x00240000u + (op << 15) + (ci << 14) +
                                  (si << 8) + (di << 3);
                    uint32_t got_result;
                    uint32_t got_ccr;
                    uint32_t exp_result;
                    uint32_t exp_ccr;

                    cpu_mul_word_reg(op, srcs[si], dsts[di], ccrs[ci],
                                     &got_result, &got_ccr);
                    exp_ccr = mul_word_ccr_ref(op, srcs[si], dsts[di],
                                               ccrs[ci], &exp_result);

                    chk32(id + 0x00u, got_result, exp_result);
                    chk32(id + 0x04u, got_ccr & 0x1fu, exp_ccr);
                }
            }
        }
    }

    mark(0x0024f000u, 0x6d750000u);
}

static uint32_t udiv32_by16_ref(uint32_t dividend, uint32_t divisor,
                                uint32_t *remainder)
{
    uint32_t quotient = 0u;
    uint32_t rem = 0u;

    for (int bit = 31; bit >= 0; --bit) {
        rem = (rem << 1) | ((dividend >> (uint32_t)bit) & 1u);
        if (rem >= divisor) {
            rem -= divisor;
            quotient |= 1u << (uint32_t)bit;
        }
    }

    *remainder = rem;
    return quotient;
}

static uint32_t signed32_magnitude(uint32_t value)
{
    return (value & 0x80000000u) ? (~value + 1u) : value;
}

static uint32_t signed16_magnitude(uint32_t value)
{
    uint32_t word = value & 0xffffu;

    return (word & 0x8000u) ? ((~word + 1u) & 0xffffu) : word;
}

static void div_word_ref(uint32_t op, uint32_t src, uint32_t dst,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *dst_result, uint32_t *ccr)
{
    uint32_t divisor = src & 0xffffu;
    uint32_t quotient;
    uint32_t remainder;
    uint32_t quotient_word;

    if (op == 0u) {
        quotient = udiv32_by16_ref(dst, divisor, &remainder);
        quotient_word = quotient & 0xffffu;
        *dst_result = ((remainder & 0xffffu) << 16) | quotient_word;
    } else {
        uint32_t dividend_neg = (dst & 0x80000000u) != 0u;
        uint32_t divisor_neg = (divisor & 0x8000u) != 0u;
        uint32_t quotient_neg = dividend_neg ^ divisor_neg;
        uint32_t dividend_mag = signed32_magnitude(dst);
        uint32_t divisor_mag = signed16_magnitude(divisor);
        uint32_t remainder_word;

        quotient = udiv32_by16_ref(dividend_mag, divisor_mag, &remainder);
        quotient_word = quotient_neg ? ((~quotient + 1u) & 0xffffu) :
                                       (quotient & 0xffffu);
        remainder_word = (dividend_neg && remainder != 0u) ?
                         ((~remainder + 1u) & 0xffffu) :
                         (remainder & 0xffffu);
        *dst_result = (remainder_word << 16) | quotient_word;
    }

    *src_result = src;
    *ccr = (initial_ccr & 0x10u) |
           ((quotient_word & 0x8000u) ? 0x08u : 0u) |
           (quotient_word == 0u ? 0x04u : 0u);
}

#define CPU_DIV_WORD_REG(OP)                                                   \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %3,%%d0\n\t"                                               \
            "move.l %4,%%d1\n\t"                                               \
            "move.l %5,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP ".w %%d0,%%d1\n\t"                                             \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2"                                                   \
            : "=m"(*src_result), "=m"(*dst_result), "=m"(*ccr)                 \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_div_word_reg(uint32_t op, uint32_t src, uint32_t dst,
                             uint32_t initial_ccr, uint32_t *src_result,
                             uint32_t *dst_result, uint32_t *ccr)
{
    if (op == 0u) {
        CPU_DIV_WORD_REG(divu);
    } else {
        CPU_DIV_WORD_REG(divs);
    }
}

#undef CPU_DIV_WORD_REG

static void test_div_word_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } divu_pairs[] = {
        {0xaaaa0001u, 0x00000000u}, {0x12340001u, 0x00000001u},
        {0xdead0007u, 0x000003e8u}, {0xbeef0002u, 0x0000ffffu},
        {0xcafe0002u, 0x00010000u}, {0x11110003u, 0x00020000u},
        {0x22228000u, 0x0000ffffu}, {0x3333ffffu, 0x0000fffeu},
        {0x4444ffffu, 0xfffe0001u},
    };
    static const struct {
        uint32_t src;
        uint32_t dst;
    } divs_pairs[] = {
        {0xaaaa0001u, 0x00000000u}, {0x11110001u, 0x00000001u},
        {0x2222ffffu, 0x00000001u}, {0x33330001u, 0xffffffffu},
        {0x4444fff9u, 0xffff15a0u}, {0x55550007u, 0xffff15a0u},
        {0x6666fff9u, 0x0000ea60u}, {0x77770002u, 0xffff8000u},
        {0x88880001u, 0xffff8000u}, {0x99990002u, 0x0000ffffu},
        {0xaaaafffeu, 0x0000ffffu}, {0xbbbb7fffu, 0x00007ffeu},
        {0xcccc0003u, 0x00007ffeu},
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        uint32_t pair_count = op == 0u ?
            (sizeof(divu_pairs) / sizeof(divu_pairs[0])) :
            (sizeof(divs_pairs) / sizeof(divs_pairs[0]));

        for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
            for (uint32_t i = 0; i < pair_count; ++i) {
                uint32_t id = 0x00250000u + (op << 15) + (ci << 14) +
                              (i << 4);
                uint32_t src = op == 0u ? divu_pairs[i].src : divs_pairs[i].src;
                uint32_t dst = op == 0u ? divu_pairs[i].dst : divs_pairs[i].dst;
                uint32_t got_src;
                uint32_t got_dst;
                uint32_t got_ccr;
                uint32_t exp_src;
                uint32_t exp_dst;
                uint32_t exp_ccr;

                cpu_div_word_reg(op, src, dst, ccrs[ci], &got_src, &got_dst,
                                 &got_ccr);
                div_word_ref(op, src, dst, ccrs[ci], &exp_src, &exp_dst,
                             &exp_ccr);

                chk32(id + 0x00u, got_src, exp_src);
                chk32(id + 0x04u, got_dst, exp_dst);
                chk32(id + 0x08u, got_ccr & 0x1fu, exp_ccr);
            }
        }
    }

    mark(0x0025f000u, 0xd1750000u);
}

static void add64_ref(uint32_t add_hi, uint32_t add_lo, uint32_t *acc_hi,
                      uint32_t *acc_lo)
{
    uint32_t old_lo = *acc_lo;

    *acc_lo = old_lo + add_lo;
    *acc_hi += add_hi + (*acc_lo < old_lo ? 1u : 0u);
}

static void mul_u32_u32_ref(uint32_t src, uint32_t dst, uint32_t *hi,
                            uint32_t *lo)
{
    uint32_t acc_hi = 0u;
    uint32_t acc_lo = 0u;
    uint32_t add_hi = 0u;
    uint32_t add_lo = src;
    uint32_t multiplier = dst;

    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if ((multiplier & 1u) != 0u) {
            add64_ref(add_hi, add_lo, &acc_hi, &acc_lo);
        }
        multiplier >>= 1;
        add_hi = (add_hi << 1) | (add_lo >> 31);
        add_lo <<= 1;
    }

    *hi = acc_hi;
    *lo = acc_lo;
}

static void negate64_ref(uint32_t *hi, uint32_t *lo)
{
    uint32_t neg_lo = ~*lo + 1u;
    uint32_t carry = neg_lo == 0u ? 1u : 0u;

    *hi = ~*hi + carry;
    *lo = neg_lo;
}

static void mul_long_ref(uint32_t op, uint32_t src, uint32_t dst,
                         uint32_t initial_ccr, uint32_t *src_result,
                         uint32_t *hi_result, uint32_t *lo_result,
                         uint32_t *ccr)
{
    uint32_t hi;
    uint32_t lo;

    if (op == 0u) {
        mul_u32_u32_ref(src, dst, &hi, &lo);
    } else {
        uint32_t src_neg = (src & 0x80000000u) != 0u;
        uint32_t dst_neg = (dst & 0x80000000u) != 0u;

        mul_u32_u32_ref(signed32_magnitude(src), signed32_magnitude(dst),
                        &hi, &lo);
        if ((src_neg ^ dst_neg) && (hi != 0u || lo != 0u)) {
            negate64_ref(&hi, &lo);
        }
    }

    *src_result = src;
    *hi_result = hi;
    *lo_result = lo;
    *ccr = (initial_ccr & 0x10u) |
           ((hi & 0x80000000u) ? 0x08u : 0u) |
           (hi == 0u && lo == 0u ? 0x04u : 0u);
}

#define CPU_MUL_LONG_REG(OP)                                                   \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %4,%%d0\n\t"                                               \
            "move.l #0xa5a55a5a,%%d1\n\t"                                      \
            "move.l %5,%%d2\n\t"                                               \
            "move.l %6,%%d3\n\t"                                               \
            "move.w %%d3,%%ccr\n\t"                                            \
            #OP ".l %%d0,%%d1:%%d2\n\t"                                        \
            "move.w %%sr,%%d3\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2\n\t"                                               \
            "move.l %%d3,%3"                                                   \
            : "=m"(*src_result), "=m"(*hi_result), "=m"(*lo_result),           \
              "=m"(*ccr)                                                       \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "d3", "cc", "memory");                       \
    } while (0)

static void cpu_mul_long_reg(uint32_t op, uint32_t src, uint32_t dst,
                             uint32_t initial_ccr, uint32_t *src_result,
                             uint32_t *hi_result, uint32_t *lo_result,
                             uint32_t *ccr)
{
    if (op == 0u) {
        CPU_MUL_LONG_REG(mulu);
    } else {
        CPU_MUL_LONG_REG(muls);
    }
}

#undef CPU_MUL_LONG_REG

static void test_mul_long_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } pairs[] = {
        {0x00000000u, 0x00000000u}, {0x00000001u, 0x00000001u},
        {0x00000002u, 0x80000000u}, {0xffffffffu, 0x00000001u},
        {0xffffffffu, 0x00000002u}, {0xffffffffu, 0xffffffffu},
        {0x00010002u, 0x00030004u}, {0x80000000u, 0x00000002u},
        {0x12345678u, 0x9abcdef0u}, {0x7fffffffu, 0x00000002u},
        {0x40000000u, 0x00000004u},
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
            for (uint32_t i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); ++i) {
                uint32_t id = 0x00260000u + (op << 15) + (ci << 14) +
                              (i << 4);
                uint32_t got_src;
                uint32_t got_hi;
                uint32_t got_lo;
                uint32_t got_ccr;
                uint32_t exp_src;
                uint32_t exp_hi;
                uint32_t exp_lo;
                uint32_t exp_ccr;

                cpu_mul_long_reg(op, pairs[i].src, pairs[i].dst, ccrs[ci],
                                 &got_src, &got_hi, &got_lo, &got_ccr);
                mul_long_ref(op, pairs[i].src, pairs[i].dst, ccrs[ci],
                             &exp_src, &exp_hi, &exp_lo, &exp_ccr);

                chk32(id + 0x00u, got_src, exp_src);
                chk32(id + 0x04u, got_hi, exp_hi);
                chk32(id + 0x08u, got_lo, exp_lo);
                chk32(id + 0x0cu, got_ccr & 0x1fu, exp_ccr);
            }
        }
    }

    mark(0x0026f000u, 0x6d750001u);
}

static void mul_long_one_ref(uint32_t op, uint32_t src, uint32_t dst,
                             uint32_t initial_ccr, uint32_t *src_result,
                             uint32_t *dst_result, uint32_t *ccr)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t v;

    if (op == 0u) {
        mul_u32_u32_ref(src, dst, &hi, &lo);
        v = hi != 0u;
    } else {
        uint32_t src_neg = (src & 0x80000000u) != 0u;
        uint32_t dst_neg = (dst & 0x80000000u) != 0u;

        mul_u32_u32_ref(signed32_magnitude(src), signed32_magnitude(dst),
                        &hi, &lo);
        if ((src_neg ^ dst_neg) && (hi != 0u || lo != 0u)) {
            negate64_ref(&hi, &lo);
        }
        v = ((lo & 0x80000000u) == 0u && hi != 0u) ||
            ((lo & 0x80000000u) != 0u && hi != 0xffffffffu);
    }

    *src_result = src;
    *dst_result = lo;
    *ccr = (initial_ccr & 0x10u) |
           ((lo & 0x80000000u) ? 0x08u : 0u) |
           (lo == 0u ? 0x04u : 0u) |
           (v ? 0x02u : 0u);
}

#define CPU_MUL_LONG_ONE_REG(OP)                                               \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %3,%%d0\n\t"                                               \
            "move.l %4,%%d1\n\t"                                               \
            "move.l %5,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP ".l %%d0,%%d1\n\t"                                             \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2"                                                   \
            : "=m"(*src_result), "=m"(*dst_result), "=m"(*ccr)                 \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_mul_long_one_reg(uint32_t op, uint32_t src, uint32_t dst,
                                 uint32_t initial_ccr, uint32_t *src_result,
                                 uint32_t *dst_result, uint32_t *ccr)
{
    if (op == 0u) {
        CPU_MUL_LONG_ONE_REG(mulu);
    } else {
        CPU_MUL_LONG_ONE_REG(muls);
    }
}

#undef CPU_MUL_LONG_ONE_REG

static void test_mul_long_one_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } pairs[] = {
        {0x00000000u, 0x00000000u}, {0x00000001u, 0x00000001u},
        {0x00000002u, 0x80000000u}, {0xffffffffu, 0x00000001u},
        {0xffffffffu, 0x00000002u}, {0xffffffffu, 0xffffffffu},
        {0x00010002u, 0x00030004u}, {0x80000000u, 0x00000001u},
        {0x80000000u, 0x00000002u}, {0x7fffffffu, 0x00000002u},
        {0x40000000u, 0x00000004u}, {0x12345678u, 0x9abcdef0u},
    };

    for (uint32_t op = 0; op < 2u; ++op) {
        for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
            for (uint32_t i = 0; i < (sizeof(pairs) / sizeof(pairs[0])); ++i) {
                uint32_t id = 0x00280000u + (op << 15) + (ci << 14) +
                              (i << 4);
                uint32_t got_src;
                uint32_t got_dst;
                uint32_t got_ccr;
                uint32_t exp_src;
                uint32_t exp_dst;
                uint32_t exp_ccr;

                cpu_mul_long_one_reg(op, pairs[i].src, pairs[i].dst, ccrs[ci],
                                     &got_src, &got_dst, &got_ccr);
                mul_long_one_ref(op, pairs[i].src, pairs[i].dst, ccrs[ci],
                                 &exp_src, &exp_dst, &exp_ccr);

                chk32(id + 0x00u, got_src, exp_src);
                chk32(id + 0x04u, got_dst, exp_dst);
                chk32(id + 0x08u, got_ccr & 0x1fu, exp_ccr);
            }
        }
    }

    mark(0x0028f000u, 0x6d750002u);
}

static void udiv64_by32_ref(uint32_t dividend_hi, uint32_t dividend_lo,
                            uint32_t divisor, uint32_t *quotient,
                            uint32_t *remainder, uint32_t *overflow)
{
    uint32_t q = 0u;
    uint32_t rem = 0u;
    uint32_t ov = 0u;

    for (int bit = 63; bit >= 0; --bit) {
        uint32_t in_bit = bit >= 32 ?
            ((dividend_hi >> (uint32_t)(bit - 32)) & 1u) :
            ((dividend_lo >> (uint32_t)bit) & 1u);
        uint32_t candidate_hi = rem >> 31;
        uint32_t candidate_lo = (rem << 1) | in_bit;

        if (candidate_hi || candidate_lo >= divisor) {
            rem = candidate_lo - divisor;
            if (bit >= 32) {
                ov = 1u;
            } else {
                q |= 1u << (uint32_t)bit;
            }
        } else {
            rem = candidate_lo;
        }
    }

    *quotient = q;
    *remainder = rem;
    *overflow = ov;
}

static void signed64_magnitude_ref(uint32_t hi, uint32_t lo, uint32_t *mag_hi,
                                   uint32_t *mag_lo)
{
    *mag_hi = hi;
    *mag_lo = lo;
    if ((hi & 0x80000000u) != 0u) {
        negate64_ref(mag_hi, mag_lo);
    }
}

static uint32_t div_long_ccr_ref(uint32_t initial_ccr, uint32_t quotient)
{
    return (initial_ccr & 0x10u) |
           ((quotient & 0x80000000u) ? 0x08u : 0u) |
           (quotient == 0u ? 0x04u : 0u);
}

static void div_long_one_ref(uint32_t op, uint32_t src, uint32_t dst,
                             uint32_t initial_ccr, uint32_t *src_result,
                             uint32_t *dst_result, uint32_t *ccr,
                             uint32_t *overflow)
{
    uint32_t quotient;
    uint32_t remainder;

    if (op == 0u) {
        udiv64_by32_ref(0u, dst, src, &quotient, &remainder, overflow);
        (void)remainder;
    } else {
        uint32_t dividend_neg = (dst & 0x80000000u) != 0u;
        uint32_t divisor_neg = (src & 0x80000000u) != 0u;
        uint32_t quotient_neg = dividend_neg ^ divisor_neg;

        udiv64_by32_ref(0u, signed32_magnitude(dst), signed32_magnitude(src),
                        &quotient, &remainder, overflow);
        (void)remainder;
        if (quotient_neg && quotient != 0u) {
            quotient = ~quotient + 1u;
        }
    }

    *src_result = src;
    *dst_result = quotient;
    *ccr = div_long_ccr_ref(initial_ccr, quotient);
}

static void div_long_pair_ref(uint32_t op, uint32_t src, uint32_t hi,
                              uint32_t lo, uint32_t initial_ccr,
                              uint32_t *src_result, uint32_t *hi_result,
                              uint32_t *lo_result, uint32_t *ccr,
                              uint32_t *overflow)
{
    uint32_t quotient;
    uint32_t remainder;

    if (op == 0u) {
        udiv64_by32_ref(hi, lo, src, &quotient, &remainder, overflow);
    } else {
        uint32_t dividend_neg = (hi & 0x80000000u) != 0u;
        uint32_t divisor_neg = (src & 0x80000000u) != 0u;
        uint32_t quotient_neg = dividend_neg ^ divisor_neg;
        uint32_t mag_hi;
        uint32_t mag_lo;

        signed64_magnitude_ref(hi, lo, &mag_hi, &mag_lo);
        udiv64_by32_ref(mag_hi, mag_lo, signed32_magnitude(src),
                        &quotient, &remainder, overflow);

        if (quotient_neg && quotient != 0u) {
            quotient = ~quotient + 1u;
        }
        if (dividend_neg && remainder != 0u) {
            remainder = ~remainder + 1u;
        }
    }

    *src_result = src;
    *hi_result = remainder;
    *lo_result = quotient;
    *ccr = div_long_ccr_ref(initial_ccr, quotient);
}

#define CPU_DIV_LONG_ONE_REG(OP)                                               \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %3,%%d0\n\t"                                               \
            "move.l %4,%%d1\n\t"                                               \
            "move.l %5,%%d2\n\t"                                               \
            "move.w %%d2,%%ccr\n\t"                                            \
            #OP ".l %%d0,%%d1\n\t"                                             \
            "move.w %%sr,%%d2\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2"                                                   \
            : "=m"(*src_result), "=m"(*dst_result), "=m"(*ccr)                 \
            : "d"(src), "d"(dst), "d"(initial_ccr)                             \
            : "d0", "d1", "d2", "cc", "memory");                             \
    } while (0)

static void cpu_div_long_one_reg(uint32_t op, uint32_t src, uint32_t dst,
                                 uint32_t initial_ccr, uint32_t *src_result,
                                 uint32_t *dst_result, uint32_t *ccr)
{
    if (op == 0u) {
        CPU_DIV_LONG_ONE_REG(divu);
    } else {
        CPU_DIV_LONG_ONE_REG(divs);
    }
}

#undef CPU_DIV_LONG_ONE_REG

#define CPU_DIV_LONG_PAIR_REG(OP)                                              \
    do {                                                                       \
        __asm__ volatile(                                                      \
            "move.l %4,%%d0\n\t"                                               \
            "move.l %5,%%d1\n\t"                                               \
            "move.l %6,%%d2\n\t"                                               \
            "move.l %7,%%d3\n\t"                                               \
            "move.w %%d3,%%ccr\n\t"                                            \
            #OP ".l %%d0,%%d1:%%d2\n\t"                                        \
            "move.w %%sr,%%d3\n\t"                                             \
            "move.l %%d0,%0\n\t"                                               \
            "move.l %%d1,%1\n\t"                                               \
            "move.l %%d2,%2\n\t"                                               \
            "move.l %%d3,%3"                                                   \
            : "=m"(*src_result), "=m"(*hi_result), "=m"(*lo_result),           \
              "=m"(*ccr)                                                       \
            : "d"(src), "d"(hi), "d"(lo), "d"(initial_ccr)                     \
            : "d0", "d1", "d2", "d3", "cc", "memory");                       \
    } while (0)

static void cpu_div_long_pair_reg(uint32_t op, uint32_t src, uint32_t hi,
                                  uint32_t lo, uint32_t initial_ccr,
                                  uint32_t *src_result, uint32_t *hi_result,
                                  uint32_t *lo_result, uint32_t *ccr)
{
    if (op == 0u) {
        CPU_DIV_LONG_PAIR_REG(divu);
    } else {
        CPU_DIV_LONG_PAIR_REG(divs);
    }
}

#undef CPU_DIV_LONG_PAIR_REG

static void test_div_long_register_differential(void)
{
    static const uint32_t ccrs[] = {0x00u, 0x1fu};
    static const struct {
        uint32_t src;
        uint32_t dst;
    } one_divu[] = {
        {0x00000001u, 0x00000000u}, {0x00000001u, 0x00000001u},
        {0x00000007u, 0x000003e8u}, {0x80000000u, 0xffffffffu},
        {0xffffffffu, 0xfffffffeu}, {0x00000003u, 0x80000000u},
        {0x00010000u, 0x80000000u},
    };
    static const struct {
        uint32_t src;
        uint32_t dst;
    } one_divs[] = {
        {0x00000001u, 0x00000000u}, {0x00000001u, 0x00000001u},
        {0xffffffffu, 0x00000001u}, {0x00000001u, 0xffffffffu},
        {0xfffffff9u, 0xffff15a0u}, {0x00000007u, 0xffff15a0u},
        {0xfffffff9u, 0x0000ea60u}, {0x00000002u, 0xffff8000u},
        {0x00000001u, 0x80000000u}, {0xfffffffeu, 0x0000ffffu},
    };
    static const struct {
        uint32_t src;
        uint32_t hi;
        uint32_t lo;
    } pair_divu[] = {
        {0x00000003u, 0x00000000u, 0x000003e8u},
        {0x00000003u, 0x00000001u, 0x00000000u},
        {0xffffffffu, 0x00000000u, 0xfffffffeu},
        {0x80000000u, 0x7fffffffu, 0xffffffffu},
        {0x10000000u, 0x00000001u, 0x00000000u},
        {0x00010000u, 0x00000000u, 0x80000000u},
        {0xfffffffeu, 0xfffffffdu, 0x00000001u},
    };
    static const struct {
        uint32_t src;
        uint32_t hi;
        uint32_t lo;
    } pair_divs[] = {
        {0x00000007u, 0xffffffffu, 0xfffffc18u},
        {0xfffffff9u, 0xffffffffu, 0xfffffc18u},
        {0xfffffff9u, 0x00000000u, 0x000003e8u},
        {0x00000003u, 0x00000001u, 0x00000000u},
        {0xfffffffdu, 0xffffffffu, 0x00000000u},
        {0x00000002u, 0xffffffffu, 0x00000000u},
        {0x00000002u, 0x00000000u, 0xffffffffu},
    };

    for (uint32_t form = 0; form < 2u; ++form) {
        for (uint32_t op = 0; op < 2u; ++op) {
            uint32_t count = form == 0u ?
                (op == 0u ? (sizeof(one_divu) / sizeof(one_divu[0])) :
                             (sizeof(one_divs) / sizeof(one_divs[0]))) :
                (op == 0u ? (sizeof(pair_divu) / sizeof(pair_divu[0])) :
                             (sizeof(pair_divs) / sizeof(pair_divs[0])));

            for (uint32_t ci = 0; ci < (sizeof(ccrs) / sizeof(ccrs[0])); ++ci) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t id = 0x00270000u + (form << 15) + (op << 14) +
                                  (ci << 13) + (i << 5);
                    uint32_t src;
                    uint32_t hi;
                    uint32_t lo;
                    uint32_t got_src;
                    uint32_t got_hi;
                    uint32_t got_lo;
                    uint32_t got_ccr;
                    uint32_t exp_src;
                    uint32_t exp_hi;
                    uint32_t exp_lo;
                    uint32_t exp_ccr;
                    uint32_t exp_overflow;

                    if (form == 0u) {
                        src = op == 0u ? one_divu[i].src : one_divs[i].src;
                        lo = op == 0u ? one_divu[i].dst : one_divs[i].dst;

                        cpu_div_long_one_reg(op, src, lo, ccrs[ci], &got_src,
                                             &got_lo, &got_ccr);
                        div_long_one_ref(op, src, lo, ccrs[ci], &exp_src,
                                         &exp_lo, &exp_ccr, &exp_overflow);

                        chk32(id + 0x00u, exp_overflow, 0u);
                        chk32(id + 0x04u, got_src, exp_src);
                        chk32(id + 0x08u, got_lo, exp_lo);
                        chk32(id + 0x0cu, got_ccr & 0x1fu, exp_ccr);
                    } else {
                        src = op == 0u ? pair_divu[i].src : pair_divs[i].src;
                        hi = op == 0u ? pair_divu[i].hi : pair_divs[i].hi;
                        lo = op == 0u ? pair_divu[i].lo : pair_divs[i].lo;

                        cpu_div_long_pair_reg(op, src, hi, lo, ccrs[ci],
                                              &got_src, &got_hi, &got_lo,
                                              &got_ccr);
                        div_long_pair_ref(op, src, hi, lo, ccrs[ci], &exp_src,
                                          &exp_hi, &exp_lo, &exp_ccr,
                                          &exp_overflow);

                        chk32(id + 0x00u, exp_overflow, 0u);
                        chk32(id + 0x04u, got_src, exp_src);
                        chk32(id + 0x08u, got_hi, exp_hi);
                        chk32(id + 0x0cu, got_lo, exp_lo);
                        chk32(id + 0x10u, got_ccr & 0x1fu, exp_ccr);
                    }
                }
            }
        }
    }

    mark(0x0027f000u, 0xd1750001u);
}

static void test_div_overflow_register_directed(void)
{
    uint32_t got0;
    uint32_t got1;
    uint32_t got2;
    uint32_t got3;

    __asm__ volatile(
        "move.l #0x00010000,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.w %%d1,%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00290000u, got0, 0x00010000u);
    chk32(0x00290004u, got1, 0x00000001u);
    chk32(0x00290008u, got2 & 0x1fu, 0x12u);

    __asm__ volatile(
        "move.l #0x00008000,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.w %%d1,%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00290010u, got0, 0x00008000u);
    chk32(0x00290014u, got1, 0x00000001u);
    chk32(0x00290018u, got2 & 0x1fu, 0x16u);

    __asm__ volatile(
        "move.l #0xffff7fff,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.w %%d1,%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00290020u, got0, 0xffff7fffu);
    chk32(0x00290024u, got1, 0x00000001u);
    chk32(0x00290028u, got2 & 0x1fu, 0x12u);

    __asm__ volatile(
        "move.l #0x80000000,%%d0\n\t"
        "moveq #-1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.l %%d1,%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "d0", "d1", "d2", "cc");
    chk32(0x00290030u, got0, 0x80000000u);
    chk32(0x00290034u, got1, 0xffffffffu);
    chk32(0x00290038u, got2 & 0x12u, 0x12u);

    __asm__ volatile(
        "moveq #1,%%d0\n\t"
        "moveq #2,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.l %%d0,%%d1:%%d2\n\t"
        "move.w %%sr,%%d3\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%d3,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "d0", "d1", "d2", "d3", "cc");
    chk32(0x00290040u, got0, 0x00000001u);
    chk32(0x00290044u, got1, 0x00000002u);
    chk32(0x00290048u, got2, 0x00000000u);
    chk32(0x0029004cu, got3 & 0x12u, 0x12u);

    __asm__ volatile(
        "moveq #1,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.l %%d0,%%d1:%%d2\n\t"
        "move.w %%sr,%%d3\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%d3,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "d0", "d1", "d2", "d3", "cc");
    chk32(0x00290050u, got0, 0x00000001u);
    chk32(0x00290054u, got1, 0x00000001u);
    chk32(0x00290058u, got2, 0x00000000u);
    chk32(0x0029005cu, got3 & 0x12u, 0x12u);

    __asm__ volatile(
        "moveq #1,%%d0\n\t"
        "moveq #-1,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.l %%d0,%%d1:%%d2\n\t"
        "move.w %%sr,%%d3\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%d3,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "d0", "d1", "d2", "d3", "cc");
    chk32(0x00290060u, got0, 0x00000001u);
    chk32(0x00290064u, got1, 0xffffffffu);
    chk32(0x00290068u, got2, 0x00000000u);
    chk32(0x0029006cu, got3 & 0x12u, 0x12u);

    mark(0x0029f000u, 0xd1e00000u);
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
    wr32(SCRATCH_BASE + 0x160, 0u);
    wr32(SCRATCH_BASE + 0x164, 0u);
    wr32(SCRATCH_BASE + 0x168, 0u);
    wr32(SCRATCH_BASE + 0x16c, 0u);
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
        "move.l %%d1,0x01ff925c\n\t"
        "move.l #0x12340003,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "mulu.w #5,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9260\n\t"
        "move.l %%d1,0x01ff9264\n\t"
        "move.l #0x5678fffd,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "muls.w #7,%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9268\n\t"
        "move.l %%d1,0x01ff926c"
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
    chk32(0x000d0050u, rd32(SCRATCH_BASE + 0x160), 0x0000000fu);
    chk32(0x000d0054u, rd32(SCRATCH_BASE + 0x164) & 0x1fu, 0x10u);
    chk32(0x000d0058u, rd32(SCRATCH_BASE + 0x168), 0xffffffebu);
    chk32(0x000d005cu, rd32(SCRATCH_BASE + 0x16c) & 0x1fu, 0x18u);
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

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0x72u) = 7u;
    __asm__ volatile(
        "lea 0x01ffa970,%%a0\n\t"
        "move.l #1000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.w 2(%%a0),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "cc", "memory");
    chk32(0x000d0170u, got0, 0x0006008eu);
    chk32(0x000d0174u, got1 & 0x1fu, 0x10u);
    chk32(0x000d0178u, got2, MUL_DIV_TEST_BASE + 0x70u);

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0x86u) = 0xfff9u;
    __asm__ volatile(
        "lea 0x01ffa980,%%a0\n\t"
        "moveq #6,%%d4\n\t"
        "move.l #-60000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.w 0(%%a0,%%d4:w),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d0180u, got0, 0xfffd217bu);
    chk32(0x000d0184u, got1 & 0x1fu, 0x10u);
    chk32(0x000d0188u, got2, MUL_DIV_TEST_BASE + 0x80u);

    wr32(MUL_DIV_TEST_BASE + 0x94u, 17u);
    __asm__ volatile(
        "lea 0x01ffa990,%%a0\n\t"
        "moveq #4,%%d4\n\t"
        "moveq #0,%%d0\n\t"
        "moveq #1,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.l 0(%%a0,%%d4:l),%%d1:%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000d0190u, got0, 0x0f0f0f0fu);
    chk32(0x000d0194u, got1, 0x00000001u);
    chk32(0x000d0198u, got2 & 0x1fu, 0x10u);
    chk32(0x000d019cu, got3, MUL_DIV_TEST_BASE + 0x90u);

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0xa2u) = 5u;
    __asm__ volatile(
        "lea 0x01ffa9a0,%%a0\n\t"
        "moveq #2,%%d4\n\t"
        "move.l #0x12340003,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "mulu.w 0(%%a0,%%d4:l),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d01a0u, got0, 0x0000000fu);
    chk32(0x000d01a4u, got1 & 0x1fu, 0x10u);
    chk32(0x000d01a8u, got2, MUL_DIV_TEST_BASE + 0xa0u);

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0xb2u) = 0xfff9u;
    __asm__ volatile(
        "lea 0x01ffa9b0,%%a0\n\t"
        "move.l #0x7fff0002,%%d4\n\t"
        "moveq #6,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "muls.w 0(%%a0,%%d4:w),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d01b0u, got0, 0xffffffd6u);
    chk32(0x000d01b4u, got1 & 0x1fu, 0x18u);
    chk32(0x000d01b8u, got2, MUL_DIV_TEST_BASE + 0xb0u);

    wr32(MUL_DIV_TEST_BASE + 0xc8u, 0x10000003u);
    __asm__ volatile(
        "lea 0x01ffa9c0,%%a0\n\t"
        "moveq #2,%%d4\n\t"
        "moveq #16,%%d0\n\t"
        "move.l #0xdeadbeef,%%d1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "mulu.l 0(%%a0,%%d4:l:4),%%d1:%%d0\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%d2,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000d01c0u, got0, 0x00000030u);
    chk32(0x000d01c4u, got1, 0x00000001u);
    chk32(0x000d01c8u, got2 & 0x1fu, 0x10u);
    chk32(0x000d01ccu, got3, MUL_DIV_TEST_BASE + 0xc0u);

    wr32(MUL_DIV_TEST_BASE + 0xd0u, 0xfffffffeu);
    __asm__ volatile(
        "lea 0x01ffa9d0,%%a0\n\t"
        "suba.l %%a1,%%a1\n\t"
        "move.l #0x00010000,%%d2\n\t"
        "move.l #0xdeadbeef,%%d3\n\t"
        "move.w #0x10,%%ccr\n\t"
        "muls.l 0(%%a0,%%a1:l),%%d3:%%d2\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d2,%0\n\t"
        "move.l %%d3,%1\n\t"
        "move.l %%d4,%2\n\t"
        "move.l %%a0,%3"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2), "=&d"(got3)
        :
        : "a0", "a1", "d2", "d3", "d4", "cc", "memory");
    chk32(0x000d01d0u, got0, 0xfffe0000u);
    chk32(0x000d01d4u, got1, 0xffffffffu);
    chk32(0x000d01d8u, got2 & 0x1fu, 0x18u);
    chk32(0x000d01dcu, got3, MUL_DIV_TEST_BASE + 0xd0u);

    wr32(MUL_DIV_TEST_BASE + 0xe4u, 0xfffffff9u);
    __asm__ volatile(
        "lea 0x01ffa9e0,%%a0\n\t"
        "moveq #1,%%d4\n\t"
        "move.l #-60000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divs.l 0(%%a0,%%d4:l:4),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d01e0u, got0, 0x0000217bu);
    chk32(0x000d01e4u, got1 & 0x1fu, 0x10u);
    chk32(0x000d01e8u, got2, MUL_DIV_TEST_BASE + 0xe0u);

    *(volatile uint16_t *)(MUL_DIV_TEST_BASE + 0xf6u) = 7u;
    __asm__ volatile(
        "lea 0x01ffa9f0,%%a0\n\t"
        "moveq #6,%%d4\n\t"
        "move.l #1000,%%d0\n\t"
        "move.w #0x10,%%ccr\n\t"
        "divu.w 0(%%a0,%%d4:w),%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d1,%1\n\t"
        "move.l %%a0,%2"
        : "=&d"(got0), "=&d"(got1), "=&d"(got2)
        :
        : "a0", "d0", "d1", "d4", "cc", "memory");
    chk32(0x000d01f0u, got0, 0x0006008eu);
    chk32(0x000d01f4u, got1 & 0x1fu, 0x10u);
    chk32(0x000d01f8u, got2, MUL_DIV_TEST_BASE + 0xf0u);
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
    uint32_t got;

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

    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    wr32(BITFIELD_TEST_BASE + 0x0c, 0u);
    wr32(BITFIELD_TEST_BASE + 0x10, 0u);
    __asm__ volatile(
        "move.l #0x89abcdef,%%d0\n\t"
        "move.l #0x01234567,%%d1\n\t"
        "bfextu %%d0{#0:#0},%%d2\n\t"
        "bfexts %%d0{#0:#0},%%d3\n\t"
        "bfins %%d1,%%d0{#0:#0}\n\t"
        "move.l %%d2,0x01ff9d08\n\t"
        "move.l %%d3,0x01ff9d0c\n\t"
        "move.l %%d0,0x01ff9d10"
        :
        :
        : "d0", "d1", "d2", "d3", "cc", "memory");
    chk32(0x000e00a4u, rd32(BITFIELD_TEST_BASE + 0x08), 0x89abcdefu);
    chk32(0x000e00a8u, rd32(BITFIELD_TEST_BASE + 0x0c), 0x89abcdefu);
    chk32(0x000e00acu, rd32(BITFIELD_TEST_BASE + 0x10), 0x01234567u);

    wr32(BITFIELD_TEST_BASE + 0x00, 0x89abcdefu);
    wr32(BITFIELD_TEST_BASE + 0x04, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x08, 0u);
    __asm__ volatile(
        "bfextu 0x01ff9d00{#0:#0},%%d0\n\t"
        "move.l %%d0,0x01ff9d08"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x000e00b0u, rd32(BITFIELD_TEST_BASE + 0x08), 0x89abcdefu);

    wr32(BITFIELD_TEST_BASE + 0x20, 0x5a000000u);
    wr32(BITFIELD_TEST_BASE + 0x24, 0xa5667788u);
    __asm__ volatile(
        "move.l #0xa1b2c3d4,%%d0\n\t"
        "bfins %%d0,0x01ff9d20{#4:#0}"
        :
        :
        : "d0", "cc", "memory");
    chk32(0x00150000u, rd32(BITFIELD_TEST_BASE + 0x20), 0x5a1b2c3du);
    chk32(0x00150004u, rd32(BITFIELD_TEST_BASE + 0x24), 0x45667788u);

    wr32(BITFIELD_TEST_BASE + 0x20, 0xffffffffu);
    wr32(BITFIELD_TEST_BASE + 0x24, 0xffaabbccu);
    __asm__ volatile("bfclr 0x01ff9d20{#4:#0}" : : : "cc", "memory");
    chk32(0x00150008u, rd32(BITFIELD_TEST_BASE + 0x20), 0xf0000000u);
    chk32(0x0015000cu, rd32(BITFIELD_TEST_BASE + 0x24), 0x0faabbccu);

    wr32(BITFIELD_TEST_BASE + 0x20, 0x50000000u);
    wr32(BITFIELD_TEST_BASE + 0x24, 0x05000000u);
    __asm__ volatile("bfset 0x01ff9d20{#4:#0}" : : : "cc", "memory");
    chk32(0x00150010u, rd32(BITFIELD_TEST_BASE + 0x20), 0x5fffffffu);
    chk32(0x00150014u, rd32(BITFIELD_TEST_BASE + 0x24), 0xf5000000u);

    wr32(BITFIELD_TEST_BASE + 0x20, 0x5a1b2c3du);
    wr32(BITFIELD_TEST_BASE + 0x24, 0x45667788u);
    __asm__ volatile("bfchg 0x01ff9d20{#4:#0}" : : : "cc", "memory");
    chk32(0x00150018u, rd32(BITFIELD_TEST_BASE + 0x20), 0x55e4d3c2u);
    chk32(0x0015001cu, rd32(BITFIELD_TEST_BASE + 0x24), 0xb5667788u);

    wr32(BITFIELD_TEST_BASE + 0x50, 0x80000000u);
    wr32(BITFIELD_TEST_BASE + 0x54, 0x00000000u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "bftst 0x01ff9d50{#4:#0}\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk32(0x00150020u, got & 0x1fu, 0x14u);

    wr32(BITFIELD_TEST_BASE + 0x50, 0x08000000u);
    wr32(BITFIELD_TEST_BASE + 0x54, 0x00000000u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "bftst 0x01ff9d50{#4:#0}\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    chk32(0x00150024u, got & 0x1fu, 0x18u);

    wr32(BITFIELD_TEST_BASE + 0x50, 0x80000000u);
    wr32(BITFIELD_TEST_BASE + 0x54, 0x00000000u);
    wr32(BITFIELD_TEST_BASE + 0x58, 0u);
    wr32(BITFIELD_TEST_BASE + 0x5c, 0u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "bfffo 0x01ff9d50{#4:#0},%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9d58\n\t"
        "move.l %%d1,0x01ff9d5c"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x00150028u, rd32(BITFIELD_TEST_BASE + 0x58), 36u);
    chk32(0x0015002cu, rd32(BITFIELD_TEST_BASE + 0x5c) & 0x1fu, 0x14u);

    wr32(BITFIELD_TEST_BASE + 0x50, 0x00400000u);
    wr32(BITFIELD_TEST_BASE + 0x54, 0x00000000u);
    wr32(BITFIELD_TEST_BASE + 0x58, 0u);
    wr32(BITFIELD_TEST_BASE + 0x5c, 0u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "bfffo 0x01ff9d50{#4:#0},%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9d58\n\t"
        "move.l %%d1,0x01ff9d5c"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x00150030u, rd32(BITFIELD_TEST_BASE + 0x58), 9u);
    chk32(0x00150034u, rd32(BITFIELD_TEST_BASE + 0x5c) & 0x1fu, 0x10u);

    wr32(BITFIELD_TEST_BASE + 0x58, 0u);
    wr32(BITFIELD_TEST_BASE + 0x5c, 0u);
    __asm__ volatile(
        "move.l #0x89abcdef,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "bfextu %%d0{#0:%%d1},%%d2\n\t"
        "move.l %%d2,0x01ff9d58\n\t"
        "move.l #0x11111111,%%d0\n\t"
        "move.l #0x76543210,%%d2\n\t"
        "moveq #0,%%d1\n\t"
        "bfins %%d2,%%d0{#0:%%d1}\n\t"
        "move.l %%d0,0x01ff9d5c"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00150040u, rd32(BITFIELD_TEST_BASE + 0x58), 0x89abcdefu);
    chk32(0x00150044u, rd32(BITFIELD_TEST_BASE + 0x5c), 0x76543210u);

    wr32(BITFIELD_TEST_BASE + 0x60, 0x01234567u);
    wr32(BITFIELD_TEST_BASE + 0x64, 0x89abcdefu);
    wr32(BITFIELD_TEST_BASE + 0x68, 0u);
    wr32(BITFIELD_TEST_BASE + 0x6c, 0u);
    __asm__ volatile(
        "moveq #0,%%d1\n\t"
        "bfextu 0x01ff9d60{#4:%%d1},%%d2\n\t"
        "move.l %%d2,0x01ff9d68\n\t"
        "moveq #4,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "bfextu 0x01ff9d60{%%d0:%%d1},%%d2\n\t"
        "move.l %%d2,0x01ff9d6c"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00150048u, rd32(BITFIELD_TEST_BASE + 0x68), 0x12345678u);
    chk32(0x0015004cu, rd32(BITFIELD_TEST_BASE + 0x6c), 0x12345678u);

    wr32(BITFIELD_TEST_BASE + 0x60, 0x5a000000u);
    wr32(BITFIELD_TEST_BASE + 0x64, 0xa5667788u);
    __asm__ volatile(
        "moveq #0,%%d1\n\t"
        "move.l #0xa1b2c3d4,%%d2\n\t"
        "bfins %%d2,0x01ff9d60{#4:%%d1}"
        :
        :
        : "d1", "d2", "cc", "memory");
    chk32(0x00150050u, rd32(BITFIELD_TEST_BASE + 0x60), 0x5a1b2c3du);
    chk32(0x00150054u, rd32(BITFIELD_TEST_BASE + 0x64), 0x45667788u);

    wr32(BITFIELD_TEST_BASE + 0x60, 0x5a000000u);
    wr32(BITFIELD_TEST_BASE + 0x64, 0xa5667788u);
    __asm__ volatile(
        "moveq #4,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "move.l #0xa1b2c3d4,%%d2\n\t"
        "bfins %%d2,0x01ff9d60{%%d0:%%d1}"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00150058u, rd32(BITFIELD_TEST_BASE + 0x60), 0x5a1b2c3du);
    chk32(0x0015005cu, rd32(BITFIELD_TEST_BASE + 0x64), 0x45667788u);

    wr32(BITFIELD_TEST_BASE + 0x70, 0xffffffffu);
    wr32(BITFIELD_TEST_BASE + 0x74, 0xffaabbccu);
    __asm__ volatile(
        "moveq #0,%%d1\n\t"
        "bfclr 0x01ff9d70{#4:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x00150060u, rd32(BITFIELD_TEST_BASE + 0x70), 0xf0000000u);
    chk32(0x00150064u, rd32(BITFIELD_TEST_BASE + 0x74), 0x0faabbccu);

    wr32(BITFIELD_TEST_BASE + 0x70, 0x50000000u);
    wr32(BITFIELD_TEST_BASE + 0x74, 0x05000000u);
    __asm__ volatile(
        "moveq #0,%%d1\n\t"
        "bfset 0x01ff9d70{#4:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x00150068u, rd32(BITFIELD_TEST_BASE + 0x70), 0x5fffffffu);
    chk32(0x0015006cu, rd32(BITFIELD_TEST_BASE + 0x74), 0xf5000000u);

    wr32(BITFIELD_TEST_BASE + 0x70, 0x5a1b2c3du);
    wr32(BITFIELD_TEST_BASE + 0x74, 0x45667788u);
    __asm__ volatile(
        "moveq #0,%%d1\n\t"
        "bfchg 0x01ff9d70{#4:%%d1}"
        :
        :
        : "d1", "cc", "memory");
    chk32(0x00150070u, rd32(BITFIELD_TEST_BASE + 0x70), 0x55e4d3c2u);
    chk32(0x00150074u, rd32(BITFIELD_TEST_BASE + 0x74), 0xb5667788u);

    wr32(BITFIELD_TEST_BASE + 0x80, 0x80000000u);
    wr32(BITFIELD_TEST_BASE + 0x84, 0x00000000u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #0,%%d1\n\t"
        "bftst 0x01ff9d80{#4:%%d1}\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x00150078u, got & 0x1fu, 0x14u);

    wr32(BITFIELD_TEST_BASE + 0x80, 0x80000000u);
    wr32(BITFIELD_TEST_BASE + 0x84, 0x00000000u);
    wr32(BITFIELD_TEST_BASE + 0x88, 0u);
    wr32(BITFIELD_TEST_BASE + 0x8c, 0u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #0,%%d1\n\t"
        "bfffo 0x01ff9d80{#4:%%d1},%%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9d88\n\t"
        "move.l %%d1,0x01ff9d8c"
        :
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x0015007cu, rd32(BITFIELD_TEST_BASE + 0x88), 36u);
    chk32(0x00150080u, rd32(BITFIELD_TEST_BASE + 0x8c) & 0x1fu, 0x14u);

    wr32(BITFIELD_TEST_BASE + 0x80, 0x00400000u);
    wr32(BITFIELD_TEST_BASE + 0x84, 0x00000000u);
    wr32(BITFIELD_TEST_BASE + 0x88, 0u);
    wr32(BITFIELD_TEST_BASE + 0x8c, 0u);
    __asm__ volatile(
        "move.w #0x1f,%%ccr\n\t"
        "moveq #4,%%d0\n\t"
        "moveq #0,%%d1\n\t"
        "bfffo 0x01ff9d80{%%d0:%%d1},%%d2\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d2,0x01ff9d88\n\t"
        "move.l %%d1,0x01ff9d8c"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00150084u, rd32(BITFIELD_TEST_BASE + 0x88), 9u);
    chk32(0x00150088u, rd32(BITFIELD_TEST_BASE + 0x8c) & 0x1fu, 0x10u);
}

static void test_movep_tas_cas_directed(void)
{
    uint32_t got;
    uint32_t got_addr;

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

    __asm__ volatile(
        "move.l #0x12345600,%%d0\n\t"
        "move.w #0,%%ccr\n\t"
        "tas %%d0\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d0,0x01ff9254\n\t"
        "move.l %%d1,%0"
        : "=d"(got)
        :
        : "d0", "d1", "cc", "memory");
    chk32(0x000f0014u, rd32(SCRATCH_BASE + 0x154), 0x12345680u);
    chk32(0x000f0018u, got & 0x1fu, 0x04u);

    wr32(SCRATCH_BASE + 0x154, 0u);
    __asm__ volatile(
        "lea 0x01ff9254,%%a0\n\t"
        "move.w #0,%%ccr\n\t"
        "tas.b (%%a0)+\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=&d"(got_addr), "=&d"(got)
        :
        : "a0", "d0", "cc", "memory");
    chk32(0x000f001au, rd32(SCRATCH_BASE + 0x154), 0x80000000u);
    chk32(0x000f001cu, got_addr, SCRATCH_BASE + 0x155u);
    chk32(0x000f001eu, got & 0x1fu, 0x04u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x30u, 0x55007f80u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x34u, 0x11223344u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x38u, 0xaabbccddu);
    for (uint32_t off = 0x40u; off <= 0x50u; off += 4u) {
        wr32(ATOMIC_RMC_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "moveq #1,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "tas.b 0x01ffae30(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffae40\n\t"
        "moveq #2,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "tas.b 0x01ffae30(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffae44\n\t"
        "moveq #3,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "tas.b 0x01ffae30(%%d4:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffae48\n\t"
        "move.l #0x7fff0004,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "tas.b 0x01ffae30(%%d4:w)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffae4c\n\t"
        "movea.l #5,%%a1\n\t"
        "move.w #0,%%ccr\n\t"
        "tas.b 0x01ffae30(%%a1:l)\n\t"
        "move.w %%sr,%%d1\n\t"
        "move.l %%d1,0x01ffae50"
        :
        :
        : "a1", "d1", "d4", "cc", "memory");
    chk32(0x000f0130u, rd32(ATOMIC_RMC_TEST_BASE + 0x30u), 0x5580ff80u);
    chk32(0x000f0134u, rd32(ATOMIC_RMC_TEST_BASE + 0x34u), 0x91a23344u);
    chk32(0x000f0138u, rd32(ATOMIC_RMC_TEST_BASE + 0x38u), 0xaabbccddu);
    chk32(0x000f013cu, rd32(ATOMIC_RMC_TEST_BASE + 0x40u) & 0x1fu, 0x14u);
    chk32(0x000f0140u, rd32(ATOMIC_RMC_TEST_BASE + 0x44u) & 0x1fu, 0x10u);
    chk32(0x000f0144u, rd32(ATOMIC_RMC_TEST_BASE + 0x48u) & 0x1fu, 0x18u);
    chk32(0x000f0148u, rd32(ATOMIC_RMC_TEST_BASE + 0x4cu) & 0x1fu, 0x00u);
    chk32(0x000f014cu, rd32(ATOMIC_RMC_TEST_BASE + 0x50u) & 0x1fu, 0x00u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x60u, 0x10203040u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x64u, 0x55667788u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x68u, 0x99aabbccu);
    for (uint32_t off = 0x70u; off <= 0x8cu; off += 4u) {
        wr32(ATOMIC_RMC_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "move.l #0xaaaa0020,%%d0\n\t"
        "move.l #0xbbbb00e1,%%d1\n\t"
        "moveq #1,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cas.b %%d0,%%d1,0x01ffae60(%%d4:l)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ffae74\n\t"
        "move.l %%d2,0x01ffae70\n\t"
        "move.l #0xaaaa0031,%%d0\n\t"
        "move.l #0xbbbb00e2,%%d1\n\t"
        "moveq #2,%%d4\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cas.b %%d0,%%d1,0x01ffae60(%%d4:l)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ffae7c\n\t"
        "move.l %%d2,0x01ffae78\n\t"
        "move.l #0xffff5566,%%d0\n\t"
        "move.l #0xccccaa55,%%d1\n\t"
        "move.l #0x7fff0004,%%d4\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.w %%d0,%%d1,0x01ffae60(%%d4:w)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ffae84\n\t"
        "move.l %%d2,0x01ffae80\n\t"
        "move.l #0xccccaa56,%%d0\n\t"
        "move.l #0xdeadbeef,%%d1\n\t"
        "movea.l #4,%%a1\n\t"
        "move.w #0x10,%%ccr\n\t"
        "cas.l %%d0,%%d1,0x01ffae60(%%a1:l)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ffae8c\n\t"
        "move.l %%d2,0x01ffae88"
        :
        :
        : "a1", "d0", "d1", "d2", "d4", "cc", "memory");
    chk32(0x000f0150u, rd32(ATOMIC_RMC_TEST_BASE + 0x60u), 0x10e13040u);
    chk32(0x000f0154u, rd32(ATOMIC_RMC_TEST_BASE + 0x64u), 0xaa557788u);
    chk32(0x000f0158u, rd32(ATOMIC_RMC_TEST_BASE + 0x68u), 0x99aabbccu);
    chk32(0x000f015cu, rd32(ATOMIC_RMC_TEST_BASE + 0x70u) & 0x1fu, 0x14u);
    chk32(0x000f0160u, rd32(ATOMIC_RMC_TEST_BASE + 0x74u), 0xaaaa0020u);
    chk32(0x000f0164u, rd32(ATOMIC_RMC_TEST_BASE + 0x78u) & 0x04u, 0x00u);
    chk32(0x000f0168u, rd32(ATOMIC_RMC_TEST_BASE + 0x7cu), 0xaaaa0030u);
    chk32(0x000f016cu, rd32(ATOMIC_RMC_TEST_BASE + 0x80u) & 0x1fu, 0x04u);
    chk32(0x000f0170u, rd32(ATOMIC_RMC_TEST_BASE + 0x84u), 0xffff5566u);
    chk32(0x000f0174u, rd32(ATOMIC_RMC_TEST_BASE + 0x88u) & 0x04u, 0x00u);
    chk32(0x000f0178u, rd32(ATOMIC_RMC_TEST_BASE + 0x8cu), 0xaa557788u);

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

    wr32(CAS2_TEST_BASE + 0x80u, 0x10203040u);
    wr32(CAS2_TEST_BASE + 0x84u, 0x55667788u);
    wr32(CAS2_TEST_BASE + 0x88u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa0010,%%d0\n\t"
        "move.l #0xbbbb00a0,%%d1\n\t"
        "cas.b %%d0,%%d1,0x01ff98c0\n\t"
        "move.l #0xaaaa0020,%%d0\n\t"
        "move.l #0xbbbb00a1,%%d1\n\t"
        "cas.b %%d0,%%d1,0x01ff98c1\n\t"
        "move.l #0xaaaa0030,%%d0\n\t"
        "move.l #0xbbbb00a2,%%d1\n\t"
        "cas.b %%d0,%%d1,0x01ff98c2\n\t"
        "move.l #0xaaaa0040,%%d0\n\t"
        "move.l #0xbbbb00a3,%%d1\n\t"
        "cas.b %%d0,%%d1,0x01ff98c3\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff98c8"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0100u, rd32(CAS2_TEST_BASE + 0x80u), 0xa0a1a2a3u);
    chk32(0x000f0104u, rd32(CAS2_TEST_BASE + 0x84u), 0x55667788u);
    chk32(0x000f0108u, rd32(CAS2_TEST_BASE + 0x88u) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0x90u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0x94u, 0u);
    wr32(CAS2_TEST_BASE + 0x98u, 0u);
    __asm__ volatile(
        "move.l #0xeeee0055,%%d0\n\t"
        "move.l #0xffff0077,%%d1\n\t"
        "cas.b %%d0,%%d1,0x01ff98d1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff98d4\n\t"
        "move.l %%d2,0x01ff98d8"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0110u, rd32(CAS2_TEST_BASE + 0x90u), 0x11223344u);
    chk32(0x000f0114u, rd32(CAS2_TEST_BASE + 0x94u), 0xeeee0022u);
    chk32(0x000f0118u, rd32(CAS2_TEST_BASE + 0x98u) & 0x04u, 0u);

    wr32(CAS2_TEST_BASE + 0xa0u, 0x12345678u);
    wr32(CAS2_TEST_BASE + 0xa4u, 0x90abcdefu);
    wr32(CAS2_TEST_BASE + 0xa8u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa1234,%%d0\n\t"
        "move.l #0xbbbbcafe,%%d1\n\t"
        "cas.w %%d0,%%d1,0x01ff98e0\n\t"
        "move.l #0xaaaa5678,%%d0\n\t"
        "move.l #0xbbbbbeef,%%d1\n\t"
        "cas.w %%d0,%%d1,0x01ff98e2\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d2,0x01ff98e8"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x000f0120u, rd32(CAS2_TEST_BASE + 0xa0u), 0xcafebeefu);
    chk32(0x000f0124u, rd32(CAS2_TEST_BASE + 0xa4u), 0x90abcdefu);
    chk32(0x000f0128u, rd32(CAS2_TEST_BASE + 0xa8u) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0xb0u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0xb4u, 0u);
    wr32(CAS2_TEST_BASE + 0xb8u, 0u);
    __asm__ volatile(
        "move.l #0xaaaa2233,%%d0\n\t"
        "move.l #0xbbbb7788,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.w %%d0,%%d1,0x01ff98f1\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff98f4\n\t"
        "move.l %%d2,0x01ff98f8"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00140000u, rd32(CAS2_TEST_BASE + 0xb0u), 0x11778844u);
    chk32(0x00140004u, rd32(CAS2_TEST_BASE + 0xb4u), 0xaaaa2233u);
    chk32(0x00140008u, rd32(CAS2_TEST_BASE + 0xb8u) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0xc0u, 0x11223344u);
    wr32(CAS2_TEST_BASE + 0xc4u, 0x55667788u);
    wr32(CAS2_TEST_BASE + 0xc8u, 0u);
    wr32(CAS2_TEST_BASE + 0xccu, 0u);
    __asm__ volatile(
        "move.l #0x22334455,%%d0\n\t"
        "move.l #0xa1b2c3d4,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.l %%d0,%%d1,0x01ff9901\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9908\n\t"
        "move.l %%d2,0x01ff990c"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00140010u, rd32(CAS2_TEST_BASE + 0xc0u), 0x11a1b2c3u);
    chk32(0x00140014u, rd32(CAS2_TEST_BASE + 0xc4u), 0xd4667788u);
    chk32(0x00140018u, rd32(CAS2_TEST_BASE + 0xc8u), 0x22334455u);
    chk32(0x0014001cu, rd32(CAS2_TEST_BASE + 0xccu) & 0x04u, 0x04u);

    wr32(CAS2_TEST_BASE + 0xd0u, 0x10203040u);
    wr32(CAS2_TEST_BASE + 0xd4u, 0x50607080u);
    wr32(CAS2_TEST_BASE + 0xd8u, 0u);
    wr32(CAS2_TEST_BASE + 0xdcu, 0u);
    __asm__ volatile(
        "move.l #0x20304051,%%d0\n\t"
        "move.l #0xaabbccdd,%%d1\n\t"
        "move.w #4,%%ccr\n\t"
        "cas.l %%d0,%%d1,0x01ff9911\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ff9918\n\t"
        "move.l %%d2,0x01ff991c"
        :
        :
        : "d0", "d1", "d2", "cc", "memory");
    chk32(0x00140020u, rd32(CAS2_TEST_BASE + 0xd0u), 0x10203040u);
    chk32(0x00140024u, rd32(CAS2_TEST_BASE + 0xd4u), 0x50607080u);
    chk32(0x00140028u, rd32(CAS2_TEST_BASE + 0xd8u), 0x20304050u);
    chk32(0x0014002cu, rd32(CAS2_TEST_BASE + 0xdcu) & 0x04u, 0u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x00u, 0x31415926u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, ATOMIC_RMC_EXPECT_CAS);
    __asm__ volatile(
        "lea 0x01ffae00,%%a0\n\t"
        "move.l #0x31415926,%%d0\n\t"
        "move.l #0x27182818,%%d1\n\t"
        "move.w #0,%%ccr\n\t"
        "cas.l %%d0,%%d1,(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,%0\n\t"
        "move.l %%d2,%1"
        : "=&d"(got), "=&d"(got_addr)
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, 0u);
    chk32(0x00160010u, rd32(ATOMIC_RMC_TEST_BASE + 0x00u), 0x27182818u);
    chk32(0x00160014u, got, 0x31415926u);
    chk32(0x00160018u, got_addr & 0x04u, 0x04u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x00u, 0x11223344u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x08u, 0u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x0cu, 0u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u,
         ATOMIC_RMC_EXPECT_CAS | ATOMIC_RMC_READ_ONLY);
    __asm__ volatile(
        "lea 0x01ffae00,%%a0\n\t"
        "move.l #0xaabbccdd,%%d0\n\t"
        "move.l #0x55667788,%%d1\n\t"
        "move.w #4,%%ccr\n\t"
        "cas.l %%d0,%%d1,(%%a0)\n\t"
        "move.w %%sr,%%d2\n\t"
        "move.l %%d0,0x01ffae08\n\t"
        "move.l %%d2,0x01ffae0c"
        :
        :
        : "a0", "d0", "d1", "d2", "cc", "memory");
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, 0u);
    chk32(0x00160100u, rd32(ATOMIC_RMC_TEST_BASE + 0x00u), 0x11223344u);
    chk32(0x00160104u, rd32(ATOMIC_RMC_TEST_BASE + 0x08u), 0x11223344u);
    chk32(0x00160108u, rd32(ATOMIC_RMC_TEST_BASE + 0x0cu) & 0x04u, 0u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x10u, 0x01020304u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, ATOMIC_RMC_EXPECT_TAS);
    __asm__ volatile(
        "move.w #0,%%ccr\n\t"
        "tas.b 0x01ffae10\n\t"
        "move.w %%sr,%%d0\n\t"
        "move.l %%d0,%0"
        : "=&d"(got)
        :
        : "d0", "cc", "memory");
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, 0u);
    chk32(0x00160020u, rd32(ATOMIC_RMC_TEST_BASE + 0x10u), 0x81020304u);
    chk32(0x00160024u, got & 0x1fu, 0u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x20u, 0x11111111u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x24u, 0x22222222u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, ATOMIC_RMC_EXPECT_CAS2);
    __asm__ volatile(
        "lea 0x01ffae20,%%a0\n\t"
        "lea 0x01ffae24,%%a1\n\t"
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
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, 0u);
    chk32(0x00160030u, rd32(ATOMIC_RMC_TEST_BASE + 0x20u), 0xaaaaaaaau);
    chk32(0x00160034u, rd32(ATOMIC_RMC_TEST_BASE + 0x24u), 0xbbbbbbbbu);
    chk32(0x00160038u, got & 0x04u, 0x04u);

    wr32(ATOMIC_RMC_TEST_BASE + 0x20u, 0x13579bdfu);
    wr32(ATOMIC_RMC_TEST_BASE + 0x24u, 0x2468ace0u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x28u, 0u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x2cu, 0u);
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u,
         ATOMIC_RMC_EXPECT_CAS2 | ATOMIC_RMC_READ_ONLY);
    __asm__ volatile(
        "lea 0x01ffae20,%%a0\n\t"
        "lea 0x01ffae24,%%a1\n\t"
        "move.l #0x13579bdf,%%d0\n\t"
        "move.l #0xaaaaaaaa,%%d1\n\t"
        "move.l #0x11111111,%%d2\n\t"
        "move.l #0xbbbbbbbb,%%d3\n\t"
        "move.w #4,%%ccr\n\t"
        "cas2.l %%d0:%%d2,%%d1:%%d3,(%%a0):(%%a1)\n\t"
        "move.w %%sr,%%d4\n\t"
        "move.l %%d2,0x01ffae28\n\t"
        "move.l %%d4,0x01ffae2c"
        :
        :
        : "a0", "a1", "d0", "d1", "d2", "d3", "d4", "cc", "memory");
    wr32(ATOMIC_RMC_TEST_BASE + 0x04u, 0u);
    chk32(0x00160120u, rd32(ATOMIC_RMC_TEST_BASE + 0x20u), 0x13579bdfu);
    chk32(0x00160124u, rd32(ATOMIC_RMC_TEST_BASE + 0x24u), 0x2468ace0u);
    chk32(0x00160128u, rd32(ATOMIC_RMC_TEST_BASE + 0x28u), 0x2468ace0u);
    chk32(0x0016012cu, rd32(ATOMIC_RMC_TEST_BASE + 0x2cu) & 0x04u, 0u);
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

    for (uint32_t off = 0xc0u; off < 0x100u; off += 4u) {
        wr32(MOVEP_TEST_BASE + off, 0u);
    }
    __asm__ volatile(
        "lea 0x01ff9cc0,%%a0\n\t"
        "move.l #0x11223344,%%d0\n\t"
        "move.l #0xa1b2c3d4,%%d1\n\t"
        "moveq #0,%%d2\n\t"
        "moveq #0,%%d3\n\t"
        "movep.l %%d0,0(%%a0)\n\t"
        "movep.w %%d1,2(%%a0)\n\t"
        "movep.l 0(%%a0),%%d2\n\t"
        "movep.w 2(%%a0),%%d3\n\t"
        "move.l %%d2,0x01ff9cf0\n\t"
        "move.l %%d3,0x01ff9cf4\n\t"
        "move.l %%a0,0x01ff9cf8"
        :
        :
        : "a0", "d0", "d1", "d2", "d3", "memory");
    chk32(0x000f00e8u, rd32(MOVEP_TEST_BASE + 0xc0), 0x1100c300u);
    chk32(0x000f00ecu, rd32(MOVEP_TEST_BASE + 0xc4), 0xd4004400u);
    chk32(0x000f00f0u, rd32(MOVEP_TEST_BASE + 0xf0), 0x11c3d444u);
    chk32(0x000f00f4u, rd32(MOVEP_TEST_BASE + 0xf4), 0x0000c3d4u);
    chk32(0x000f00f8u, rd32(MOVEP_TEST_BASE + 0xf8), MOVEP_TEST_BASE + 0xc0u);
}

void kmain(void)
{
    delay_poll_window();
    uart_puts("CORETEST START\n");
    progress_char('0');

#ifdef CORETEST_SIM_FOCUS_STACK
    test_stack_frame_control_directed();
#elif defined(CORETEST_SIM_FOCUS_SDRAM_BERR)
    test_sdram_combined_write_berr();
#elif defined(CORETEST_SIM_FOCUS_UNARY_ARITH)
    test_unary_arith_register_differential();
#elif defined(CORETEST_SIM_FOCUS_ROTATE)
    test_rotate_register_differential();
#elif defined(CORETEST_SIM_FOCUS_DIV)
    test_div_word_register_differential();
    progress_char('D');
    test_div_overflow_register_directed();
    progress_char('V');
#elif defined(CORETEST_SIM_FOCUS_MOVEP_TAS_CAS)
    test_movep_tas_cas_directed();
#elif defined(CORETEST_SIM_FOCUS_MOVES)
    test_moves_directed();
#elif defined(CORETEST_SIM_FOCUS_PMMU)
    test_pmmu_register_directed();
    progress_char('P');
#elif defined(CORETEST_SIM_FOCUS_SYNTH_CLEANUP)
    test_aligned_long();
    test_unaligned_lanes_asm();
    test_address_arithmetic_directed();
    progress_char('a');
    test_shift_register_differential();
    progress_char('s');
    test_rotate_register_differential();
    progress_char('r');
    test_system_control_directed();
    progress_char('c');
    test_pmmu_register_directed();
    progress_char('P');
#ifdef CORETEST_SIM_IRQ
    test_interrupt_autovector_directed();
    progress_char('I');
#endif
#elif defined(CORETEST_SIM_FOCUS_IRQ)
#ifdef CORETEST_SIM_IRQ
    test_vesta_timer_vectored();
    progress_char('V');
    test_interrupt_autovector_directed();
    progress_char('I');
#endif
#elif defined(CORETEST_SIM_FOCUS_EXCEPTION_RECOVERY)
    test_exception_recovery_directed();
    progress_char('e');
#elif defined(CORETEST_SIM_FOCUS_POST3)
    progress_char('x');
    test_addx_subx_cmpm_memory_directed();
    progress_char('z');
    progress_char('y');
    test_system_control_directed();
    progress_char('Y');
#ifndef CORETEST_SIM_SKIP_MOVES
    progress_char('u');
    test_moves_directed();
    progress_char('U');
#endif
    progress_char('q');
    test_cmp2_chk2_directed();
    progress_char('Q');
    progress_char('k');
    test_chk_directed();
    progress_char('K');
    progress_char('c');
    test_cas2_directed();
    progress_char('C');
    progress_char('r');
    test_return_control_directed();
    progress_char('R');
#elif defined(CORETEST_SIM_FOCUS_POST4)
    test_bcd_register_differential();
    progress_char('B');
    test_bcd_directed();
    progress_char('C');
    test_pack_unpk_register_differential();
    progress_char('P');
    test_pack_unpk_directed();
    progress_char('p');
#ifndef CORETEST_SIM_SKIP_EXCEPTION_RECOVERY
    test_exception_recovery_directed();
    progress_char('e');
#endif
#if defined(CORETEST_SIM_IRQ) && !defined(CORETEST_SIM_SKIP_IRQ)
    test_vesta_timer_vectored();
    progress_char('v');
    test_interrupt_autovector_directed();
    progress_char('I');
#endif
    test_alu_shift_bitfield_bcd_directed();
    progress_char('A');
    test_condition_codes_directed();
    progress_char('N');
    test_condition_consumers_directed();
    progress_char('n');
    test_bitops_register_differential();
    progress_char('O');
    test_bitops_directed();
    progress_char('o');
    test_mul_word_register_differential();
    progress_char('M');
    test_div_word_register_differential();
    progress_char('D');
    test_mul_long_register_differential();
    progress_char('m');
    test_mul_long_one_register_differential();
    progress_char('1');
    test_div_long_register_differential();
    progress_char('d');
    test_div_overflow_register_directed();
    progress_char('V');
    test_signed_mul_div_directed();
    progress_char('S');
    test_mul_div_memory_directed();
    progress_char('s');
    test_memory_bitfield_directed();
    progress_char('F');
    test_memory_bitfield_extended_directed();
    progress_char('f');
    test_movep_tas_cas_directed();
    progress_char('T');
    test_movep_displacement_directed();
#else
    test_aligned_long();
    test_byte_lanes_c();
    test_word_lanes_c();
    test_unaligned_lanes_asm();
    test_absolute_indexed_stores();
    test_full_format_indexed_memory_ops();
    test_indexed_ea_scale_directed();
    test_pc_indexed_data_directed();
    progress_char('1');
    test_an_indexed_stores();
    test_an_post_pre_byte();
    test_memory_to_memory_move_directed();
    test_movem_directed();
    test_control_flow_directed();
    test_stack_frame_control_directed();
    progress_char('2');
    test_address_arithmetic_directed();
    progress_char('a');
    test_register_transform_directed();
    progress_char('b');
    test_unary_logic_directed();
    progress_char('c');
    test_immediate_alu_directed();
    progress_char('d');
    test_data_alu_indexed_directed();
    progress_char('e');
    test_data_alu_register_differential();
    progress_char('f');
    test_xalu_register_differential();
    progress_char('g');
    test_unary_arith_register_differential();
    progress_char('h');
    test_unary_logic_register_differential();
    progress_char('i');
    test_shift_register_differential();
    progress_char('j');
    test_rotate_register_differential();
    progress_char('3');
    test_addx_subx_cmpm_memory_directed();
    test_system_control_directed();
    test_pmmu_register_directed();
    progress_char('W');
#ifndef CORETEST_SIM_SKIP_MOVES
    test_moves_directed();
#endif
    test_cmp2_chk2_directed();
    test_chk_directed();
    test_cas2_directed();
    test_return_control_directed();
    progress_char('4');
    test_bcd_register_differential();
    progress_char('B');
    test_bcd_directed();
    progress_char('C');
    test_pack_unpk_register_differential();
    progress_char('P');
    test_pack_unpk_directed();
    progress_char('p');
#ifndef CORETEST_SIM_SKIP_EXCEPTION_RECOVERY
    test_exception_recovery_directed();
    progress_char('e');
#endif
#if defined(CORETEST_SIM_IRQ) && !defined(CORETEST_SIM_SKIP_IRQ)
    test_vesta_timer_vectored();
    progress_char('v');
    test_interrupt_autovector_directed();
    progress_char('I');
#endif
    test_alu_shift_bitfield_bcd_directed();
    progress_char('A');
    test_condition_codes_directed();
    progress_char('N');
    test_condition_consumers_directed();
    progress_char('n');
    test_bitops_register_differential();
    progress_char('O');
    test_bitops_directed();
    progress_char('o');
    test_mul_word_register_differential();
    progress_char('M');
    test_div_word_register_differential();
    progress_char('D');
    test_mul_long_register_differential();
    progress_char('m');
    test_mul_long_one_register_differential();
    progress_char('1');
    test_div_long_register_differential();
    progress_char('d');
    test_div_overflow_register_directed();
    progress_char('V');
    test_signed_mul_div_directed();
    progress_char('S');
    test_mul_div_memory_directed();
    progress_char('s');
    test_memory_bitfield_directed();
    progress_char('F');
    test_memory_bitfield_extended_directed();
    progress_char('f');
    test_movep_tas_cas_directed();
    progress_char('T');
    test_movep_displacement_directed();
#endif
    progress_char('!');

    for (;;) {
        uart_puts("CORETEST PASS sum=");
        uart_hex32(g_sum);
        uart_putc('\n');
        delay_poll_window();
    }
}
