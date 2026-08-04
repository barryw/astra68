// Astraea — DMA / blitter / copper / arbiter register interface for Astra 68.
// Mirror of docs/ASTRAEA.md (v0.4). Keep in sync with that spec.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_ASTRAEA_H
#define ASTRA_ASTRAEA_H

#include <stddef.h>
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
    uint32_t CAPS;           // 0x018
    uint32_t _r0;            // 0x01C
    uint32_t _r1[8];         // 0x020..0x03C reserved in v0.4
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
    uint32_t BLIT_FENCE;     // 0x070
    uint32_t _r2[3];         // 0x074..0x07C
    // copper control
    uint32_t COP_CTRL;       // 0x080
    uint32_t COP_START;      // 0x084
    uint32_t COP_STATUS;     // 0x088
    uint32_t COP_STROBE;     // 0x08C
    uint32_t COP_IRQ_SRC;    // 0x090
    uint32_t _r3[(0x0100 - 0x094) / 4];
    // draw / glyph / bounded flood frontend
    uint32_t DRAW_DST;          // 0x100
    uint32_t DRAW_DST_PITCH;    // 0x104
    uint32_t DRAW_FORMAT;       // 0x108
    uint32_t DRAW_CLIP_MIN;     // 0x10C
    uint32_t DRAW_CLIP_MAX;     // 0x110, exclusive
    uint32_t DRAW_P0;           // 0x114
    uint32_t DRAW_P1;           // 0x118
    uint32_t DRAW_RADII;        // 0x11C
    uint32_t DRAW_FG;           // 0x120
    uint32_t DRAW_BG;           // 0x124
    uint32_t DRAW_PATTERN_HI;   // 0x128
    uint32_t DRAW_PATTERN_LO;   // 0x12C
    uint32_t DRAW_ORIGIN;       // 0x130
    uint32_t DRAW_SRC;          // 0x134
    uint32_t DRAW_SRC_PITCH;    // 0x138
    uint32_t DRAW_SRC_SIZE;     // 0x13C
    uint32_t DRAW_PALETTE;      // 0x140
    uint32_t DRAW_WORK;         // 0x144
    uint32_t DRAW_WORK_ENTRIES; // 0x148
    uint32_t DRAW_OP;           // 0x14C
    uint32_t DRAW_CTRL;         // 0x150
    uint32_t DRAW_STATUS;       // 0x154
    uint32_t DRAW_FENCE;        // 0x158
    uint32_t _r4[(0x4000 - 0x15C) / 4];
    CopInsn  COP[2048];      // 0x4000..0x7FFF
} AstraeaRegs;

#define ASTRAEA ((AstraeaRegs *)ASTRAEA_BASE)

#define ASTRAEA_ID_MAGIC 0x41535452u   // "ASTR"
#define ASTRAEA_VERSION_0_4 0x00040000u

// ---- Capability bits (offset 0x018) ----
#define ASTRAEA_CAP_COPY      (1u << 0)
#define ASTRAEA_CAP_FILL      (1u << 1)
#define ASTRAEA_CAP_COPY_KEY  (1u << 2)
#define ASTRAEA_CAP_COPY_MASK (1u << 3)
#define ASTRAEA_CAP_GEOMETRY  (1u << 4)
#define ASTRAEA_CAP_GLYPH     (1u << 5)
#define ASTRAEA_CAP_FLOOD     (1u << 6)
#define ASTRAEA_CAP_COPPER    (1u << 7)

// ---- IRQ bits (IRQ_EN / IRQ_STAT) ----
#define ASTRAEA_IRQ_BLIT_DONE     (1u << 0)
#define ASTRAEA_IRQ_COPPER        (1u << 1)
#define ASTRAEA_IRQ_DRAW_DONE     (1u << 3)

// ---- BLIT_OP ----
#define BLIT_MODE_COPY      0u
#define BLIT_MODE_FILL      1u
#define BLIT_MODE_COPY_KEY  2u
#define BLIT_MODE_COPY_MASK 3u
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
#define BLIT_ERROR_INVALID_CONFIG 1u
#define BLIT_ERROR_INTERNAL       2u
#define BLIT_ERROR_PROTECTED      5u

#define BLIT_DIM_(w, h) (((uint32_t)(h) << 16) | ((w) & 0xFFFF))

// ---- COP_CTRL / COP_STATUS ----
#define COP_ENABLE      (1u << 0)
#define COP_VBL_RESTART (1u << 1)
#define COP_RUNNING     (1u << 16)   // COP_STATUS
#define COP_WAITING     (1u << 17)   // COP_STATUS

// ---- DRAW_FORMAT / DRAW_OP ----
#define DRAW_FORMAT_INDEX8  0u
#define DRAW_FORMAT_RGB565  1u

#define DRAW_OP_LINE          0u
#define DRAW_OP_RECT          1u
#define DRAW_OP_RECT_FILL     2u
#define DRAW_OP_CIRCLE        3u
#define DRAW_OP_CIRCLE_FILL   4u
#define DRAW_OP_ELLIPSE       5u
#define DRAW_OP_ELLIPSE_FILL  6u
#define DRAW_OP_PATTERN_FILL  7u
#define DRAW_OP_GLYPH_MASK1   8u
#define DRAW_OP_GLYPH_A4      9u
#define DRAW_OP_GLYPH_INDEX4 10u
#define DRAW_OP_GLYPH_INDEX8 11u
#define DRAW_OP_FLOOD_FILL   12u
#define DRAW_OP_OPAQUE_BACKGROUND (1u << 8)
#define DRAW_OP_TRANSPARENT_INDEX(index) (((uint32_t)(index) & 0xffu) << 16)

// ---- DRAW_CTRL / DRAW_STATUS ----
#define DRAW_START   (1u << 0)
#define DRAW_IRQ_EN  (1u << 1)
#define DRAW_BUSY    (1u << 0)
#define DRAW_DONE    (1u << 1)
#define DRAW_ERROR   (0xffu << 8)
#define DRAW_ERROR_CODE(stat) (((stat) >> 8) & 0xffu)
#define DRAW_ERROR_INVALID_CONFIG 1u
#define DRAW_ERROR_INTERNAL       2u
#define DRAW_ERROR_WORK_OVERFLOW  3u
#define DRAW_ERROR_ADDRESS_RANGE  4u
#define DRAW_ERROR_PROTECTED      5u

#define DRAW_XY_(x, y) \
    ((((uint32_t)(uint16_t)(y)) << 16) | (uint16_t)(x))
#define DRAW_SIZE_(w, h) \
    ((((uint32_t)(uint16_t)(h)) << 16) | (uint16_t)(w))
#define DRAW_RADII_(rx, ry) DRAW_SIZE_((rx), (ry))

// A glyph command with DRAW_WORK_ENTRIES != 0 reads this 16-byte array.
// source_offset is added to DRAW_SRC; source/destination positions and size
// use the same packed y:x and h:w representation as the MMIO registers.
typedef struct AstraeaGlyphDescriptor {
    uint32_t source_offset;
    uint32_t source_position;
    uint32_t destination_position;
    uint32_t size;
} AstraeaGlyphDescriptor;

_Static_assert(offsetof(AstraeaRegs, CAPS) == 0x018u,
               "Astraea capability ABI offset");
_Static_assert(offsetof(AstraeaRegs, DRAW_DST) == 0x100u,
               "Astraea draw ABI offset");
_Static_assert(sizeof(AstraeaGlyphDescriptor) == 16u,
               "Astraea glyph descriptor ABI size");

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
