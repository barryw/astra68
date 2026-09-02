// Vega — video chip register interface for Astra 68.
// Vega device ABI. Keep in sync with the active graphics implementation.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_VEGA_H
#define ASTRA_VEGA_H

#include <stddef.h>
#include <stdint.h>

#define VEGA_BASE 0xFFF20000u

// ---- Sprite entry (0x20 bytes) ----
typedef volatile struct {
    uint32_t CTRL;    // +0x00  SPR_* bits
    uint32_t POS;     // +0x04  [31:16] Y (s16), [15:0] X (s16)
    uint32_t SIZE;    // +0x08  [31:16] height, [15:0] width (px)
    uint32_t BASE;    // +0x0C  [24:0] SDRAM addr of 4bpp pattern
    uint32_t PITCH;   // +0x10  [15:0] bytes per pattern row
    uint32_t _rsv[3]; // +0x14  reserved
} VegaSprite;

// ---- Register block ----
typedef volatile struct {
    uint32_t ID;            // 0x000
    uint32_t VERSION;       // 0x004
    uint32_t CTRL;          // 0x008
    uint32_t STATUS;        // 0x00C
    uint32_t IRQ_EN;        // 0x010
    uint32_t IRQ_STAT;      // 0x014
    uint32_t MODE;          // 0x018
    uint32_t CAPS;          // 0x01C
    uint32_t BEAM;          // 0x020
    uint32_t RASTER_CMP;    // 0x024
    uint32_t ACTIVE;        // 0x028
    uint32_t _r1;           // 0x02C
    uint32_t BACKDROP;      // 0x030
    uint32_t SCENE_GENERATION; // 0x034
    uint32_t DRAW_FENCE;       // 0x038
    uint32_t BLIT_FENCE;       // 0x03C
    uint32_t FB_BASE;       // 0x040
    uint32_t FB_PITCH;      // 0x044
    uint32_t FB_FORMAT;     // 0x048
    uint32_t FB_COLORKEY;   // 0x04C
    uint32_t PRESENT_CTRL;                 // 0x050
    uint32_t PRESENT_STATUS;               // 0x054
    uint32_t PRESENT_COMPLETED_GENERATION; // 0x058
    uint32_t PRESENT_COMPLETED_FRAME;      // 0x05C
    uint32_t PRESENT_RETIRED_FB;           // 0x060
    uint32_t FRAME_COUNTER;                // 0x064
    uint32_t FB_VIEW;       // 0x068 [31:16] Y, [15:0] X
    uint32_t FB_VIRTUAL;    // 0x06C [31:16] height, [15:0] width
    uint32_t FB_WRAP;       // 0x070 bit 0 X, bit 1 Y
    uint32_t _r4[(0x400 - 0x074) / 4];
    uint32_t PAL[256];      // 0x400..0x7FC
    uint32_t SPR_CTRL;      // 0x800
    uint32_t SPR_BUDGET;    // 0x804
    uint32_t SPR_COLLISION; // 0x808
    uint32_t _r5[(0x1000 - 0x80C) / 4];
    VegaSprite SPR[16];     // 0x1000..0x11FF
} VegaRegs;

#define VEGA ((VegaRegs *)VEGA_BASE)

#define VEGA_ID_MAGIC 0x56454741u   // "VEGA"
#define VEGA_VERSION_0_5 0x00050000u

// Boot-only text aperture. One ASCII/CP437 byte per cell, row-major, 90x30.
// The OS may ignore/disable this plane and render its own bitmap fonts.
#define VEGA_POST_TEXT_BASE (VEGA_BASE + 0x2000u)
#define VEGA_POST_TEXT ((volatile uint8_t *)VEGA_POST_TEXT_BASE)
#define VEGA_POST_COLS 90u
#define VEGA_POST_ROWS 30u
#define VEGA_CAP_POST_TEXT (1u << 0)
#define VEGA_CAP_FRAMEBUFFER (1u << 1)
#define VEGA_CAP_PALETTE     (1u << 2)
#define VEGA_CAP_TILEMAP     (1u << 3)
#define VEGA_CAP_SPRITE      (1u << 4)
#define VEGA_CAP_INDEX8      (1u << 5)
#define VEGA_CAP_FB_SCROLL   (1u << 6)

// ---- VEGA_CTRL ----
#define VEGA_CTRL_DISPLAY_EN  (1u << 0)
#define VEGA_CTRL_FB_EN       (1u << 1)
#define VEGA_CTRL_SPR_EN      (1u << 2)
#define VEGA_CTRL_COLORKEY_EN (1u << 3)
#define VEGA_CTRL_BACKDROP_EN (1u << 4)

// ---- VEGA_STATUS ----
#define VEGA_STAT_VBLANK       (1u << 0)
#define VEGA_STAT_HBLANK       (1u << 1)
#define VEGA_STAT_SCENE_LOCKED  (1u << 2)
#define VEGA_STAT_FLIP_PENDING  VEGA_STAT_SCENE_LOCKED
#define VEGA_STAT_SPR_OVERFLOW (1u << 3)
#define VEGA_STAT_DISPLAY_READY (1u << 4)
#define VEGA_STAT_UNDERRUN      (1u << 5) // sticky; write 1 to clear
#define VEGA_STAT_CONFIG_ERROR  (1u << 6)
#define VEGA_STAT_FETCH_BUSY    (1u << 7)

// ---- VEGA_PRESENT_CTRL / VEGA_PRESENT_STATUS ----
#define VEGA_PRESENT_SUBMIT                (1u << 0)
#define VEGA_PRESENT_PENDING               (1u << 0)
#define VEGA_PRESENT_COPY_BUSY             (1u << 1)
#define VEGA_PRESENT_DONE                  (1u << 2)
#define VEGA_PRESENT_INVALID               (1u << 3)
#define VEGA_PRESENT_SHADOW_WRITE_REJECTED (1u << 4)
#define VEGA_PRESENT_COPY_DEADLINE         (1u << 5)
#define VEGA_PRESENT_WAIT_WRITERS          (1u << 6)
#define VEGA_PRESENT_STICKY_MASK \
    (VEGA_PRESENT_DONE | VEGA_PRESENT_INVALID | \
     VEGA_PRESENT_SHADOW_WRITE_REJECTED | VEGA_PRESENT_COPY_DEADLINE)

// ---- VEGA_IRQ_EN / VEGA_IRQ_STAT ----
#define VEGA_IRQ_VBLANK    (1u << 0)
#define VEGA_IRQ_RASTER    (1u << 1)
#define VEGA_IRQ_COLLISION (1u << 2)

// ---- VEGA_MODE ----
#define VEGA_MODE_720x480 0
#define VEGA_MODE_640x480 1
#define VEGA_MODE_320x240 2
#define VEGA_MODE_320x200 3
#define VEGA_MODE_400x300 4
#define VEGA_MODE_640x400 5

// ---- VEGA_FB_FORMAT ----
#define VEGA_FMT_RGB565 0
#define VEGA_FMT_INDEX8 1

// ---- Framebuffer viewport ----
#define VEGA_FB_VIEW_(x, y) \
    (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
#define VEGA_FB_VIRTUAL_(width, height) \
    (((uint32_t)(uint16_t)(height) << 16) | (uint16_t)(width))
#define VEGA_FB_WRAP_X (1u << 0)
#define VEGA_FB_WRAP_Y (1u << 1)
#define VEGA_FB_WRAP_XY (VEGA_FB_WRAP_X | VEGA_FB_WRAP_Y)

// ---- Sprite CTRL bits / fields ----
#define SPR_ENABLE     (1u << 0)
#define SPR_VISIBLE    (1u << 1)
#define SPR_FLIPX      (1u << 2)
#define SPR_FLIPY      (1u << 3)
#define SPR_BEHIND     (1u << 4)
#define SPR_COLLIDE_EN (1u << 5)
#define SPR_PRIORITY_SHIFT 8
#define SPR_PRIORITY_MASK  (0xFu << 8)
#define SPR_PAL_BANK_SHIFT 12
#define SPR_PAL_BANK_MASK  (0xFu << 12)
#define SPR_TRANSP_SHIFT   16
#define SPR_TRANSP_MASK    (0xFu << 16)

// ---- VEGA_SPR_CTRL (global) ----
#define VEGA_SPR_CTRL_ENABLE (1u << 0)
#define VEGA_SPRITE_COUNT 16u
#define VEGA_SPR_BUDGET_INDEX8_MAX 1024u
#define VEGA_SPR_BUDGET_RGB565_MAX  512u

// ---- Packing helpers ----
#define VEGA_POS(x, y)   (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
#define VEGA_SIZE(w, h)  (((uint32_t)(h) << 16) | (uint16_t)(w))
#define VEGA_RGB(r, g, b) \
    (((uint32_t)(uint8_t)(r) << 16) | ((uint32_t)(uint8_t)(g) << 8) | (uint8_t)(b))
#define VEGA_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F)))

_Static_assert(offsetof(VegaRegs, FB_VIEW) == 0x068u,
               "Vega viewport ABI offset");
_Static_assert(offsetof(VegaRegs, PAL) == 0x400u,
               "Vega palette ABI offset");
_Static_assert(offsetof(VegaRegs, SPR_CTRL) == 0x800u,
               "Vega sprite-control ABI offset");
_Static_assert(offsetof(VegaRegs, SPR) == 0x1000u,
               "Vega sprite descriptor ABI offset");

#endif // ASTRA_VEGA_H
