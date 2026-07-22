// Vesta — system / MMU / IRQ / timers / I/O register interface for Astra 68.
// Mirror of docs/VESTA.md. Keep in sync with that specification.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_VESTA_H
#define ASTRA_VESTA_H

#include <stddef.h>
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
    uint32_t CPU_CYCLES_LO;      // 0x0EC; read latches the coherent 64-bit value
    uint32_t CPU_CYCLES_HI;      // 0x0F0; upper half of the LO-read snapshot
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
    uint32_t _perf_r0;             // 0x12C
    uint32_t HOST_CTRL;            // 0x130 AstraHost boot request
    uint32_t HOST_STATUS;          // 0x134 HOST_* status bits
    uint32_t HOST_ROM_SIZE;        // 0x138 validated payload bytes
    uint32_t HOST_ROM_CRC32;       // 0x13C validated payload CRC32
    uint32_t HOST_INITIAL_SP;      // 0x140 captured stage-2 vector
    uint32_t HOST_INITIAL_PC;      // 0x144 captured stage-2 vector
    uint32_t HOST_BYTES_RECEIVED;  // 0x148 streaming progress
    uint32_t HOST_ERROR;           // 0x14C protocol status code
    uint32_t BLOCK_ID;              // 0x150 "HOST"
    uint32_t BLOCK_VERSION;         // 0x154
    uint32_t BLOCK_CAPS;            // 0x158
    uint32_t BLOCK_STATE;           // 0x15C
    uint32_t BLOCK_MEDIA_GEN;       // 0x160
    uint32_t BLOCK_MEDIA_SIZE_HI;   // 0x164 sectors
    uint32_t BLOCK_MEDIA_SIZE_LO;   // 0x168 sectors
    uint32_t BLOCK_QUEUE;           // 0x16C queue status
    uint32_t BLOCK_REQ_ID;          // 0x170
    uint32_t BLOCK_REQ_OP;          // 0x174 flags[15:8], op[7:0]
    uint32_t BLOCK_REQ_LBA_HI;      // 0x178 partition-relative LBA
    uint32_t BLOCK_REQ_LBA_LO;      // 0x17C
    uint32_t BLOCK_REQ_SECTORS;     // 0x180 low 16 bits
    uint32_t BLOCK_REQ_BUFFER;      // 0x184 physical SDRAM address
    uint32_t BLOCK_REQ_SUBMIT;      // 0x188 write bit 0
    uint32_t BLOCK_CPL_ID;          // 0x18C
    uint32_t BLOCK_CPL_STATUS;      // 0x190 status[31:16], sectors[15:0]
    uint32_t BLOCK_CPL_DETAIL;      // 0x194 backend detail
    uint32_t BLOCK_CPL_MEDIA_GEN;   // 0x198
    uint32_t BLOCK_CPL_HOST_GEN;    // 0x19C
    uint32_t BLOCK_CPL_POP;         // 0x1A0 write bit 0
    uint32_t BLOCK_ERROR;           // 0x1A4 RW1C submit errors
    uint32_t BLOCK_HOST_GEN;        // 0x1A8
    uint32_t BLOCK_STATE_ACK;       // 0x1AC write bit 0
    uint32_t BLOCK_MAX_SECTORS;     // 0x1B0
    uint32_t _r1[(0x200 - 0x1B4) / 4];
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
    uint32_t UART_RX_STATUS; // 0x508; bit 1 is RW1C
    uint32_t UART_RX_DATA;   // 0x50C; read pops one byte
    uint32_t _r4[(0x600 - 0x510) / 4];
    // SPI / SD 0x600
    uint32_t SPI_CTRL;       // 0x600
    uint32_t SPI_STATUS;     // 0x604
    uint32_t SPI_DATA;       // 0x608
    uint32_t _r5[(0x700 - 0x60C) / 4];
    // input 0x700
    uint32_t INPUT_ID;          // 0x700 "INPT"
    uint32_t INPUT_VERSION;     // 0x704
    uint32_t INPUT_CAPS;        // 0x708
    uint32_t INPUT_STATUS;      // 0x70C
    uint32_t INPUT_HEADER;      // 0x710
    uint32_t INPUT_VALUE;       // 0x714
    uint32_t INPUT_TIMESTAMP;   // 0x718 milliseconds
    uint32_t INPUT_DEVICE_SEQ;  // 0x71C device[31:16], sequence[15:0]
    uint32_t INPUT_HOST_GEN;    // 0x720
    uint32_t INPUT_POP;         // 0x724 write bit 0
} VestaRegs;

#define VESTA ((VestaRegs *)VESTA_BASE)

#define VESTA_ID_MAGIC 0x56535441u   // "VSTA"
#define VESTA_VERSION_1_0 0x00010000u

// ---- CPU_MODEL / CPU_IMPL / CPU_FEATURES ----
#define CPU_MODEL_68030 0x00068030u
#define CPU_IMPL_TGM2   0x54474D32u   // "TGM2"
// Retired published values remain reserved and must never be reused.
#define CPU_MODEL_68020 0x00068020u
#define CPU_IMPL_WF30   0x57463330u   // "WF30" (retired)
#define CPU_IMPL_TG20   0x54473230u   // "TG20" (retired)
#define CPU_IMPL_TG30   0x54473330u   // "TG30" (retired)
#define CPU_FEAT_PMMU   (1u << 0)
#define CPU_FEAT_FPU    (1u << 1)
#define CPU_FEAT_DATA32 (1u << 2)
#define CPU_FEAT_ADDR32 (1u << 3)

// ---- SYS_STATUS ----
#define SYS_SDRAM_PRESENT (1u << 0)
#define SYS_SDRAM_READY   (1u << 1)
#define SYS_BOOT_OVERLAY  (1u << 2)
#define SYS_VIDEO_READY   (1u << 3)
#define SYS_SD_CONTROLLER (1u << 4)
#define SYS_ASTRA_HOST    (1u << 5)
#define SYS_USB_READY     (1u << 6)
#define SYS_USB_DMA_FAULT (1u << 7)

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
#define SYS_BOOT_SDRAM  (1u << 1)
#define SYS_HOST_BOOT_REQUEST (1u << 2)
#define RESET_POWERON   0u
#define RESET_SOFT      1u
#define RESET_WATCHDOG  2u

// ---- SPI / SD ----
#define SPI_CTRL_CS_N          (1u << 0)
#define SPI_CTRL_CLKDIV_SHIFT  4
#define SPI_CTRL_CLKDIV(value) (((value) & 0xfu) << SPI_CTRL_CLKDIV_SHIFT)
#define SPI_STATUS_BUSY        (1u << 0)

// ---- AstraHost ----
#define HOST_BOOT_REQUESTED (1u << 0)
#define HOST_BOOT_BUSY      (1u << 1)
#define HOST_BOOT_DONE      (1u << 2)
#define HOST_BOOT_ERROR     (1u << 3)
#define HOST_LINK_SEEN      (1u << 7)

// ---- AstraHost runtime block service ----
#define BLOCK_ID_MAGIC 0x484F5354u // "HOST"
#define BLOCK_VERSION_1_0 0x00010000u
#define BLOCK_CAP_READ  (1u << 0)
#define BLOCK_CAP_WRITE (1u << 1)
#define BLOCK_CAP_FLUSH (1u << 2)
#define BLOCK_STATE_LINK_UP       (1u << 0)
#define BLOCK_STATE_MEDIA_PRESENT (1u << 1)
#define BLOCK_STATE_WRITE_ENABLE  (1u << 2)
#define BLOCK_QUEUE_COMPLETION_VALID (1u << 20)
#define BLOCK_QUEUE_COMPLETION_LEVEL(v) (((v) >> 12) & 0x1Fu)
#define BLOCK_QUEUE_REQUEST_READY (1u << 8)
#define BLOCK_QUEUE_REQUEST_LEVEL(v) ((v) & 0x1Fu)
#define BLOCK_OP_READ  1u
#define BLOCK_OP_WRITE 2u
#define BLOCK_OP_FLUSH 3u
#define BLOCK_SUBMIT (1u << 0)
#define BLOCK_CPL_POP_BIT (1u << 0)
#define BLOCK_STATE_ACK_BIT (1u << 0)
#define BLOCK_ERROR_BAD_OP        (1u << 0)
#define BLOCK_ERROR_BAD_COUNT     (1u << 1)
#define BLOCK_ERROR_BAD_BUFFER    (1u << 2)
#define BLOCK_ERROR_NO_MEDIA      (1u << 3)
#define BLOCK_ERROR_WRITE_PROTECT (1u << 4)
#define BLOCK_ERROR_LBA_RANGE     (1u << 5)
#define BLOCK_ERROR_QUEUE_FULL    (1u << 6)
#define BLOCK_ERROR_BAD_ID        (1u << 7)
#define BLOCK_ERROR_BAD_FLAGS     (1u << 8)

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
#define IRQ_SRC_STORAGE  4
#define IRQ_SRC_INPUT    5
#define IRQ_SRC_RESERVED6 6
#define IRQ_SRC_USB       7
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

// ---- Generic input queue ----
#define INPUT_ID_MAGIC 0x494E5054u // "INPT"
#define INPUT_VERSION_1_0 0x00010000u
#define INPUT_CAP_KEYBOARD (1u << 0)
#define INPUT_CAP_POINTER  (1u << 1)
#define INPUT_CAP_GAMEPAD  (1u << 2)
#define INPUT_EVENT_VALID  (1u << 8)
#define INPUT_EVENT_LEVEL(v) ((v) & 0x1Fu)
#define INPUT_EVENT_CLASS(v) (((v) >> 24) & 0xFFu)
#define INPUT_EVENT_KIND(v) (((v) >> 16) & 0xFFu)
#define INPUT_EVENT_FLAGS(v) ((v) & 0xFFFFu)
#define INPUT_EVENT_DEVICE(v) (((v) >> 16) & 0xFFFFu)
#define INPUT_EVENT_SEQUENCE(v) ((v) & 0xFFFFu)
#define INPUT_POP_BIT (1u << 0)

// ---- UART_STATUS ----
#define UART_TX_READY   (1u << 0)
#define UART_TX_BUSY    (1u << 1)

// ---- UART RX path (host -> FPGA) ----
#define UART_RXSTATUS (*(volatile uint32_t *)(VESTA_BASE + 0x508u))
#define UART_RXDATA   (*(volatile uint32_t *)(VESTA_BASE + 0x50Cu))  // read consumes the byte
#define UART_RX_READY (1u << 0)
#define UART_RX_FIFO_OVERRUN (1u << 1)
#define UART_RX_FIFO_LEVEL(v) (((v) >> 8) & 0xFFu)

// ---- SPI ----
#define SPI_CS_N      (1u << 0)
#define SPI_CLKDIV(n) (((n) & 0xF) << 4)
#define SPI_BUSY      (1u << 0)

_Static_assert(offsetof(VestaRegs, BLOCK_ID) == 0x150u,
               "Vesta block-service ABI offset");
_Static_assert(offsetof(VestaRegs, IRQ_PENDING) == 0x300u,
               "Vesta IRQ ABI offset");
_Static_assert(offsetof(VestaRegs, TIMER) == 0x400u,
               "Vesta timer ABI offset");
_Static_assert(offsetof(VestaRegs, INPUT_ID) == 0x700u,
               "Vesta input ABI offset");
_Static_assert(offsetof(VestaRegs, INPUT_POP) == 0x724u,
               "Vesta input-pop ABI offset");

#endif // ASTRA_VESTA_H
