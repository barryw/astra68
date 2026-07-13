// Astraea — DMA / blitter / copper / arbiter register interface for Astra 68.
// Mirror of docs/ASTRAEA.md (v0.1). Keep in sync with that spec.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_ASTRAEA_H
#define ASTRA_ASTRAEA_H

#include <stdint.h>

#define ASTRAEA_BASE 0xFFF10000u
#define CHIPSET_BASE 0xFFF00000u   // copper MOVE offsets are relative to this

// ---- Copper instruction (8 bytes) ----
typedef volatile struct {
    uint32_t w0;   // [31:29] opcode, [28:0] A-field
    uint32_t w1;   // B-field (value / X)
} CopInsn;

// ---- Register block ----
typedef volatile struct {
    uint32_t ID;             // 0x000
    uint32_t VERSION;        // 0x004
    uint32_t CTRL;           // 0x008
    uint32_t STATUS;         // 0x00C
    uint32_t IRQ_EN;         // 0x010
    uint32_t IRQ_STAT;       // 0x014
    uint32_t _r0[2];         // 0x018..0x01C
    // arbiter
    uint32_t ARB_CTRL;       // 0x020
    uint32_t ARB_STATUS;     // 0x024
    uint32_t ARB_PERF;       // 0x028
    uint32_t _r1[5];         // 0x02C..0x03C
    // blitter
    uint32_t BLIT_SRC;       // 0x040
    uint32_t BLIT_DST;       // 0x044
    uint32_t BLIT_MASK;      // 0x048
    uint32_t BLIT_SRC_PITCH; // 0x04C
    uint32_t BLIT_DST_PITCH; // 0x050
    uint32_t BLIT_MASK_PITCH;// 0x054
    uint32_t BLIT_DIM;       // 0x058
    uint32_t BLIT_OP;        // 0x05C
    uint32_t BLIT_COLOR;     // 0x060
    uint32_t BLIT_KEY;       // 0x064
    uint32_t BLIT_CTRL;      // 0x068
    uint32_t BLIT_STATUS;    // 0x06C
    uint32_t _r2[4];         // 0x070..0x07C
    // copper control
    uint32_t COP_CTRL;       // 0x080
    uint32_t COP_START;      // 0x084
    uint32_t COP_STATUS;     // 0x088
    uint32_t COP_STROBE;     // 0x08C
    uint32_t _r3[(0x4000 - 0x090) / 4];
    CopInsn  COP[2048];      // 0x4000..0x7FFF
} AstraeaRegs;

#define ASTRAEA ((AstraeaRegs *)ASTRAEA_BASE)

#define ASTRAEA_ID_MAGIC 0x41535452u   // "ASTR"

// ---- IRQ bits (IRQ_EN / IRQ_STAT) ----
#define ASTRAEA_IRQ_BLIT_DONE     (1u << 0)
#define ASTRAEA_IRQ_COPPER        (1u << 1)
#define ASTRAEA_IRQ_ARB_UNDERRUN  (1u << 2)

// ---- ARB_STATUS ----
#define ARB_VIDEO_UNDERRUN (1u << 0)
#define ARB_AUDIO_UNDERRUN (1u << 1)

// ---- BLIT_OP ----
#define BLIT_MODE_COPY      0u
#define BLIT_MODE_FILL      1u
#define BLIT_MODE_COPY_KEY  2u
#define BLIT_MODE_COPY_MASK 3u
#define BLIT_MODE_LINE      4u   // deferred
#define BLIT_ELEM8   (0u << 4)
#define BLIT_ELEM16  (1u << 4)
#define BLIT_ELEM32  (2u << 4)
#define BLIT_REV_X   (1u << 8)
#define BLIT_REV_Y   (1u << 9)

// ---- BLIT_CTRL / BLIT_STATUS ----
#define BLIT_START   (1u << 0)
#define BLIT_IRQ_EN  (1u << 1)
#define BLIT_BUSY    (1u << 0)
#define BLIT_DONE    (1u << 1)
#define BLIT_ERROR   (0xffu << 8)
#define BLIT_ERROR_CODE(stat) (((stat) >> 8) & 0xffu)

#define BLIT_DIM_(w, h) (((uint32_t)(h) << 16) | ((w) & 0xFFFF))

// ---- COP_CTRL / COP_STATUS ----
#define COP_ENABLE      (1u << 0)
#define COP_VBL_RESTART (1u << 1)
#define COP_RUNNING     (1u << 16)   // COP_STATUS
#define COP_WAITING     (1u << 17)   // COP_STATUS

// ---- Copper opcodes (w0[31:29]) + operand helpers ----
#define COP_OP_END   (0u << 29)
#define COP_OP_MOVE  (1u << 29)
#define COP_OP_WAIT  (2u << 29)
#define COP_OP_SKIP  (3u << 29)
#define COP_OP_IRQ   (4u << 29)
#define COP_OP_JUMP  (5u << 29)

// register offset for MOVE: pass the register's absolute address
// (via uintptr_t so it's exact on the 32-bit m68k target and clean on 64-bit hosts)
#define COP_OFF(addr) (((uint32_t)(uintptr_t)(addr) - CHIPSET_BASE) & 0x3FFFFu)

#endif // ASTRA_ASTRAEA_H
