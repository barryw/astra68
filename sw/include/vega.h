// Vega — video chip register interface for Astra 68.
// Mirror of docs/VEGA.md (v0.3). Keep in sync with that spec.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_VEGA_H
#define ASTRA_VEGA_H

#include <stdint.h>

#define VEGA_BASE 0xFFF20000u

// ---- Tilemap layer (0x20 bytes) ----
typedef volatile struct {
    uint32_t CTRL;    // +0x00  TILE_* bits
    uint32_t MAP;     // +0x04  [24:0] SDRAM addr of tilemap (16-bit entries)
    uint32_t SET;     // +0x08  [24:0] SDRAM addr of tileset (4bpp)
    uint32_t SIZE;    // +0x0C  [3:0] mapW log2, [7:4] mapH log2
    uint32_t SCROLL;  // +0x10  [31:16] scrollY, [15:0] scrollX (px)
    uint32_t _rsv[3]; // +0x14  reserved (row-scroll/affine, future)
} VegaTile;

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
    uint32_t _r2[3];        // 0x034..0x03C
    uint32_t FB_BASE;       // 0x040
    uint32_t FB_PITCH;      // 0x044
    uint32_t FB_FORMAT;     // 0x048
    uint32_t FB_COLORKEY;   // 0x04C
    uint32_t _r3[(0x080 - 0x050) / 4];
    VegaTile TILE[2];       // 0x080..0x0BF
    uint32_t _r4[(0x400 - 0x0C0) / 4];
    uint32_t PAL[256];      // 0x400..0x7FC
    uint32_t SPR_CTRL;      // 0x800
    uint32_t SPR_BUDGET;    // 0x804
    uint32_t SPR_COLLISION; // 0x808
    uint32_t _r5[(0x1000 - 0x80C) / 4];
    VegaSprite SPR[32];     // 0x1000..0x13FF
} VegaRegs;

#define VEGA ((VegaRegs *)VEGA_BASE)

#define VEGA_ID_MAGIC 0x56454741u   // "VEGA"

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

// ---- VEGA_CTRL ----
#define VEGA_CTRL_DISPLAY_EN  (1u << 0)
#define VEGA_CTRL_FB_EN       (1u << 1)
#define VEGA_CTRL_SPR_EN      (1u << 2)
#define VEGA_CTRL_COLORKEY_EN (1u << 3)
#define VEGA_CTRL_BACKDROP_EN (1u << 4)

// ---- VEGA_STATUS ----
#define VEGA_STAT_VBLANK       (1u << 0)
#define VEGA_STAT_HBLANK       (1u << 1)
#define VEGA_STAT_FLIP_PENDING (1u << 2)
#define VEGA_STAT_SPR_OVERFLOW (1u << 3)
#define VEGA_STAT_DISPLAY_READY (1u << 4)
#define VEGA_STAT_UNDERRUN      (1u << 5) // sticky; write 1 to clear
#define VEGA_STAT_CONFIG_ERROR  (1u << 6)
#define VEGA_STAT_FETCH_BUSY    (1u << 7)

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

// ---- TILE_CTRL bits ----
#define TILE_ENABLE    (1u << 0)
#define TILE_TRANSP_EN (1u << 1)
#define TILE_16        (1u << 2)   // 16x16 tiles (else 8x8)
#define TILE_WRAP_X    (1u << 3)
#define TILE_ABOVE     (1u << 4)   // foreground (above sprites/FB)
#define TILE_WRAP_Y    (1u << 5)
#define TILE_WRAP      (TILE_WRAP_X | TILE_WRAP_Y)
#define TILE_TRANSP_SHIFT 16
#define TILE_TRANSP_MASK  (0xFu << 16)

// TILE_SIZE / TILE_SCROLL packers, and a tilemap entry builder (16-bit, SDRAM).
#define TILE_MAPSIZE(logw, logh) ((((logh) & 0xF) << 4) | ((logw) & 0xF))
#define TILE_SCROLL(x, y)        (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
#define TILE_ENTRY(idx, bank, flipx, flipy)                 \
    ((uint16_t)(((idx) & 0x3FF) | (((bank) & 7) << 10) |    \
                ((flipx) ? 0x4000u : 0u) | ((flipy) ? 0x8000u : 0u)))

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
#define VEGA_SPR_BUDGET_INDEX8_MAX 1024u
#define VEGA_SPR_BUDGET_RGB565_MAX  512u

// ---- Packing helpers ----
#define VEGA_POS(x, y)   (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
#define VEGA_SIZE(w, h)  (((uint32_t)(h) << 16) | (uint16_t)(w))
#define VEGA_RGB(r, g, b) \
    (((uint32_t)(uint8_t)(r) << 16) | ((uint32_t)(uint8_t)(g) << 8) | (uint8_t)(b))
#define VEGA_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F)))

#endif // ASTRA_VEGA_H
