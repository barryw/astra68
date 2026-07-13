// Vesta — system / MMU / IRQ / timers / I/O register interface for Astra 68.
// Mirror of docs/VESTA.md (v0.1). Keep in sync with that spec.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_VESTA_H
#define ASTRA_VESTA_H

#include <stdint.h>

#define VESTA_BASE 0xFFF00000u

typedef volatile struct {   // MMU region (0x10 bytes)
    uint32_t BASE;   // logical base
    uint32_t SIZE;   // bytes
    uint32_t PHYS;   // physical base
    uint32_t ATTR;   // RGN_* bits
} VestaRegion;

typedef volatile struct {   // timer (0x10 bytes)
    uint32_t LOAD;
    uint32_t VALUE;
    uint32_t CTRL;
    uint32_t STATUS;
} VestaTimer;

typedef volatile struct {   // discoverable hardware personality (0x10 bytes)
    uint32_t ID;             // four-character identifier
    uint32_t VERSION;        // major.minor in 16.16 form
    uint32_t BASE;           // physical MMIO base
    uint32_t SIZE;           // MMIO aperture size in bytes
} VestaPersonality;

typedef volatile struct {
    // system control 0x000
    uint32_t ID;             // 0x000
    uint32_t VERSION;        // 0x004
    uint32_t MACHINE_ID;     // 0x008
    uint32_t SYS_CTRL;       // 0x00C
    uint32_t SYS_STATUS;     // 0x010
    uint32_t RESET_REASON;   // 0x014
    uint32_t SCRATCH;        // 0x018
    uint32_t CPU_MODEL;      // 0x01C
    uint32_t CPU_IMPL;       // 0x020
    uint32_t CPU_FEATURES;   // 0x024
    uint32_t CPU_HZ;         // 0x028
    uint32_t RAM_BASE;       // 0x02C
    uint32_t RAM_SIZE;       // 0x030
    uint32_t ROM_BASE;       // 0x034
    uint32_t ROM_SIZE;       // 0x038
    uint32_t BUILD_ID;       // 0x03C
    uint32_t PERSONALITY_COUNT;  // 0x040
    uint32_t PERSONALITY_STRIDE; // 0x044
    uint32_t NVRAM_CAPS;     // 0x048
    uint32_t _identity_r0;   // 0x04C
    VestaPersonality PERSONALITY[8]; // 0x050..0x0CF
    uint32_t MEMTEST_CTRL;       // 0x0D0
    uint32_t MEMTEST_STATUS;     // 0x0D4
    uint32_t MEMTEST_PROGRESS;   // 0x0D8 byte offset
    uint32_t MEMTEST_ERRORS;     // 0x0DC
    uint32_t MEMTEST_FIRST_FAIL; // 0x0E0 byte offset
    uint32_t MEMTEST_EXPECTED;   // 0x0E4 low byte
    uint32_t MEMTEST_ACTUAL;     // 0x0E8 low byte
    uint32_t CPU_CYCLES_LO;      // 0x0EC free-running CPU/bus clocks
    uint32_t CPU_CYCLES_HI;      // 0x0F0
    uint32_t ICACHE_HITS;         // 0x0F4 TG wrapper instruction-cache hits
    uint32_t ICACHE_MISSES;       // 0x0F8 TG wrapper instruction-cache misses
    uint32_t DCACHE_HITS;         // 0x0FC TG wrapper data-cache hits
    // MMU control 0x100
    uint32_t MMU_CTRL;       // 0x100
    uint32_t MMU_FAULT_ADDR; // 0x104
    uint32_t MMU_FAULT_STAT; // 0x108
    uint32_t MMU_FAULT_ACK;  // 0x10C
    uint32_t DCACHE_MISSES;       // 0x110 TG wrapper data-cache misses
    uint32_t CPU_SDRAM_READS;     // 0x114 completed CPU SDRAM read requests
    uint32_t CPU_SDRAM_WRITES;    // 0x118 completed CPU SDRAM write requests
    uint32_t CPU_SDRAM_WAIT;      // 0x11C clocks spent waiting for CPU SDRAM
    uint32_t SDRAM_LINE_HITS;      // 0x120 external 16-byte line-buffer hits
    uint32_t SDRAM_LINE_MISSES;    // 0x124 external 16-byte line fills
    uint32_t SDRAM_POSTED_WRITES;  // 0x128 writes acknowledged before SDRAM completion
    uint32_t _r1[(0x200 - 0x12C) / 4];
    // region table 0x200
    VestaRegion REGION[16];  // 0x200..0x2FF
    // interrupt controller 0x300
    uint32_t IRQ_PENDING;    // 0x300
    uint32_t IRQ_ENABLE;     // 0x304
    uint32_t IRQ_SOFT;       // 0x308
    uint32_t IRQ_ACK;        // 0x30C
    uint32_t IRQ_CURRENT;    // 0x310
    uint32_t _r2[(0x380 - 0x314) / 4];
    uint32_t IRQ_CFG[32];    // 0x380..0x3FF
    // timers 0x400
    VestaTimer TIMER[2];     // 0x400..0x41F
    uint32_t _r3[(0x500 - 0x420) / 4];
    // UART 0x500
    uint32_t UART_DATA;      // 0x500
    uint32_t UART_STATUS;    // 0x504
    uint32_t UART_CTRL;      // 0x508
    uint32_t UART_BAUD;      // 0x50C
    uint32_t _r4[(0x600 - 0x510) / 4];
    // SPI / SD 0x600
    uint32_t SPI_CTRL;       // 0x600
    uint32_t SPI_STATUS;     // 0x604
    uint32_t SPI_DATA;       // 0x608
    uint32_t _r5[(0x700 - 0x60C) / 4];
    // input 0x700
    uint32_t PAD0;           // 0x700
    uint32_t PAD1;           // 0x704
    uint32_t KEY_DATA;       // 0x708
    uint32_t KEY_STATUS;     // 0x70C
    uint32_t INPUT_CTRL;     // 0x710
} VestaRegs;

#define VESTA ((VestaRegs *)VESTA_BASE)

#define VESTA_ID_MAGIC 0x56535441u   // "VSTA"
#define VESTA_VERSION_1_0 0x00010000u

// ---- CPU_MODEL / CPU_IMPL / CPU_FEATURES ----
#define CPU_MODEL_68020 0x00068020u
#define CPU_MODEL_68030 0x00068030u
#define CPU_IMPL_WF30   0x57463330u   // "WF30"
#define CPU_IMPL_TG20   0x54473230u   // "TG20"
#define CPU_IMPL_TG30   0x54473330u   // "TG30"
#define CPU_IMPL_TGM2   0x54474D32u   // "TGM2"
#define CPU_FEAT_PMMU   (1u << 0)
#define CPU_FEAT_FPU    (1u << 1)
#define CPU_FEAT_DATA32 (1u << 2)
#define CPU_FEAT_ADDR32 (1u << 3)

// ---- SYS_STATUS ----
#define SYS_SDRAM_PRESENT (1u << 0)
#define SYS_SDRAM_READY   (1u << 1)
#define SYS_BOOT_OVERLAY  (1u << 2)
#define SYS_VIDEO_READY   (1u << 3)

// ---- SDRAM power-on self-test ----
#define MEMTEST_START       (1u << 0)
#define MEMTEST_BUSY        (1u << 0)
#define MEMTEST_DONE        (1u << 1)
#define MEMTEST_FAILED      (1u << 2)
#define MEMTEST_PHASE_SHIFT 8
#define MEMTEST_PHASE(stat) (((stat) >> MEMTEST_PHASE_SHIFT) & 7u)
#define MEMTEST_PHASE_WRITE 1u
#define MEMTEST_PHASE_READ  2u
#define MEMTEST_PHASE_DONE  3u

// ---- SYS_CTRL / RESET_REASON ----
#define SYS_SOFT_RESET  (1u << 0)
#define RESET_POWERON   0u
#define RESET_SOFT      1u
#define RESET_WATCHDOG  2u

// ---- MMU_CTRL ----
#define MMU_ENABLE       (1u << 0)
#define MMU_SUPER_BYPASS (1u << 1)

// ---- MMU_FAULT_STAT ----
#define MMUF_FAULT     (1u << 0)
#define MMUF_NO_REGION (1u << 1)
#define MMUF_PERM_R    (1u << 2)
#define MMUF_PERM_W    (1u << 3)
#define MMUF_PERM_X    (1u << 4)
#define MMUF_USER      (1u << 5)
#define MMUF_REGION(stat) (((stat) >> 8) & 0x1F)

// ---- RGN_ATTR ----
#define RGN_ENABLE (1u << 0)
#define RGN_R      (1u << 1)
#define RGN_W      (1u << 2)
#define RGN_X      (1u << 3)
#define RGN_USER   (1u << 4)
#define RGN_RW  (RGN_R | RGN_W)
#define RGN_RX  (RGN_R | RGN_X)
#define RGN_RWX (RGN_R | RGN_W | RGN_X)

// ---- Interrupt sources (bit index / IRQ_CFG index) ----
#define IRQ_SRC_TIMER0   0
#define IRQ_SRC_TIMER1   1
#define IRQ_SRC_UART_RX  2
#define IRQ_SRC_UART_TX  3
#define IRQ_SRC_SD       4
#define IRQ_SRC_KEYBOARD 5
#define IRQ_SRC_GAMEPAD  6
#define IRQ_SRC_VEGA     8
#define IRQ_SRC_ASTRAEA  9
#define IRQ_SRC_LYRA     10
#define IRQ_BIT(src) (1u << (src))

// ---- IRQ_CFG[i] ----
#define IRQ_CFG_LEVEL(l)  ((l) & 7u)
#define IRQ_CFG_VECTOR(v) (((v) & 0xFFu) << 8)
#define IRQ_CFG_EDGE      (1u << 16)

// ---- TMR_CTRL / TMR_STATUS ----
#define TMR_ENABLE      (1u << 0)
#define TMR_PERIODIC    (1u << 1)
#define TMR_IRQ_EN      (1u << 2)
#define TMR_PRESCALE(n) (((n) & 0xF) << 4)
#define TMR_EXPIRED     (1u << 0)

// ---- UART_STATUS / UART_CTRL ----
#define UART_TX_READY   (1u << 0)
#define UART_RX_VALID   (1u << 1)
#define UART_TX_BUSY    (1u << 2)
#define UART_RX_OVERRUN (1u << 3)
#define UART_TX_IRQ_EN  (1u << 0)
#define UART_RX_IRQ_EN  (1u << 1)

// ---- UART RX path (host -> FPGA) ----
// On the SoC these two addresses READ back RX status/data; they alias the
// UART_CTRL/UART_BAUD WRITE-side registers above (dual-function). See astra_soc.sv.
#define UART_RXSTATUS (*(volatile uint32_t *)(VESTA_BASE + 0x508u))  // [0] = RX_READY
#define UART_RXDATA   (*(volatile uint32_t *)(VESTA_BASE + 0x50Cu))  // read consumes the byte
#define UART_RX_READY (1u << 0)

// ---- SPI ----
#define SPI_CS_N      (1u << 0)
#define SPI_CLKDIV(n) (((n) & 0xF) << 4)
#define SPI_BUSY      (1u << 0)

#endif // ASTRA_VESTA_H
