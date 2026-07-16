// Destructive visual acceptance ROM for the complete Vega/Astraea path.
#include "vesta.h"
#include "vega.h"
#include "astraea.h"

#define SDRAM_CPU_BASE 0x02000000u
#define FB_OFFSET       0x00000000u
#define TILE0_MAP       0x00080000u
#define TILE1_MAP       0x00081000u
#define TILE0_SET       0x00090000u
#define TILE1_SET       0x00091000u
#define SPRITE_SET      0x00092000u
#define SPRITE_STRESS_SET 0x00100000u
#define GLYPH_MASK      0x00093000u
#define GLYPH_A4        0x00093100u
#define BLIT_SRC_TEST   0x00094000u
#define BLIT_DST_TEST   0x00094100u
#define BLIT_MASK_TEST  0x00094200u
#define RGB_SCRATCH     0x00095000u
#define FLOOD_WORK      0x00096000u

#define PANEL_BASE      0xfff01000u
#define PANEL_LED_DATA  (*(volatile uint32_t *)(PANEL_BASE + 0x18u))
#define PANEL_LED_OWNER (*(volatile uint32_t *)(PANEL_BASE + 0x1cu))

#define TIMEOUT 20000000u

#ifndef DEMO_TILE1_ENABLE
#define DEMO_TILE1_ENABLE 1
#endif
#ifndef DEMO_TILE0_ENABLE
#define DEMO_TILE0_ENABLE 1
#endif
#ifndef DEMO_STRESS_SPRITES
#define DEMO_STRESS_SPRITES 0
#endif
#ifndef DEMO_STRESS_RGB565
#define DEMO_STRESS_RGB565 0
#endif
#ifndef DEMO_STRESS_BUDGET
#define DEMO_STRESS_BUDGET VEGA_SPR_BUDGET_INDEX8_MAX
#endif

static uint32_t draw_fence = 1u;

static volatile uint8_t *ram8(uint32_t offset)
{
    return (volatile uint8_t *)(SDRAM_CPU_BASE + offset);
}

static volatile uint16_t *ram16(uint32_t offset)
{
    return (volatile uint16_t *)(SDRAM_CPU_BASE + offset);
}

static void set_leds(uint8_t value)
{
    PANEL_LED_OWNER = 0xffu;
    PANEL_LED_DATA = value;
}

static void fail(uint8_t stage)
{
    VEGA->TILE[0].CTRL = 0u;
    VEGA->TILE[1].CTRL = 0u;
    VEGA->SPR_CTRL = 0u;
    VEGA->BACKDROP = VEGA_RGB(255, 0, 0);
    VEGA->CTRL = VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_BACKDROP_EN;
    set_leds((uint8_t)(0x80u | (stage & 0x7fu)));
    for (;;) {}
}

static int wait_sdram(void)
{
    uint32_t timeout = TIMEOUT;
    while (!(VESTA->SYS_STATUS & SYS_SDRAM_READY) && timeout != 0u)
        --timeout;
    return timeout != 0u;
}

static int blit(uint32_t src, uint32_t dst, uint32_t mask,
                uint16_t src_pitch, uint16_t dst_pitch, uint16_t mask_pitch,
                uint16_t width, uint16_t height, uint32_t operation,
                uint32_t color, uint32_t key)
{
    uint32_t timeout = TIMEOUT;
    ASTRAEA->BLIT_SRC = src;
    ASTRAEA->BLIT_DST = dst;
    ASTRAEA->BLIT_MASK = mask;
    ASTRAEA->BLIT_SRC_PITCH = src_pitch;
    ASTRAEA->BLIT_DST_PITCH = dst_pitch;
    ASTRAEA->BLIT_MASK_PITCH = mask_pitch;
    ASTRAEA->BLIT_DIM = BLIT_DIM_(width, height);
    ASTRAEA->BLIT_OP = operation;
    ASTRAEA->BLIT_COLOR = color;
    ASTRAEA->BLIT_KEY = key;
    ASTRAEA->BLIT_CTRL = BLIT_START;
    while (!(ASTRAEA->BLIT_STATUS & BLIT_DONE) && timeout != 0u)
        --timeout;
    return timeout != 0u &&
           BLIT_ERROR_CODE(ASTRAEA->BLIT_STATUS) == 0u;
}

static int draw(uint32_t operation)
{
    uint32_t timeout = TIMEOUT;
    uint32_t fence = draw_fence++;
    ASTRAEA->DRAW_OP = operation;
    ASTRAEA->DRAW_FENCE = fence;
    ASTRAEA->DRAW_CTRL = DRAW_START;
    while (!(ASTRAEA->DRAW_STATUS & DRAW_DONE) && timeout != 0u)
        --timeout;
    return timeout != 0u &&
           DRAW_ERROR_CODE(ASTRAEA->DRAW_STATUS) == 0u &&
           ASTRAEA->DRAW_FENCE == fence;
}

static int test_blitter(void)
{
    volatile uint8_t *src = ram8(BLIT_SRC_TEST);
    volatile uint8_t *dst = ram8(BLIT_DST_TEST);
    volatile uint8_t *mask = ram8(BLIT_MASK_TEST);
    uint32_t i;

    for (i = 0; i < 64u; ++i) {
        src[i] = (uint8_t)(i + 1u);
        dst[i] = 0u;
    }
    if (!blit(BLIT_SRC_TEST, BLIT_DST_TEST, 0u, 64u, 64u, 0u,
              64u, 1u, BLIT_MODE_COPY | BLIT_ELEM8, 0u, 0u))
        return 0;
    for (i = 0; i < 64u; ++i)
        if (dst[i] != (uint8_t)(i + 1u)) return 0;

    for (i = 0; i < 16u; ++i) {
        src[i] = (uint8_t)(i & 3u);
        dst[i] = 0xeeu;
    }
    if (!blit(BLIT_SRC_TEST, BLIT_DST_TEST, 0u, 16u, 16u, 0u,
              16u, 1u, BLIT_MODE_COPY_KEY | BLIT_ELEM8, 0u, 0u))
        return 0;
    for (i = 0; i < 16u; ++i) {
        uint8_t expected = (i & 3u) == 0u ? 0xeeu : (uint8_t)(i & 3u);
        if (dst[i] != expected) return 0;
    }

    mask[0] = 0xa5u;
    for (i = 0; i < 8u; ++i) {
        src[i] = (uint8_t)(0x30u + i);
        dst[i] = 0xaau;
    }
    if (!blit(BLIT_SRC_TEST, BLIT_DST_TEST, BLIT_MASK_TEST,
              8u, 8u, 1u, 8u, 1u,
              BLIT_MODE_COPY_MASK | BLIT_ELEM8, 0u, 0u))
        return 0;
    for (i = 0; i < 8u; ++i) {
        uint8_t expected = (0xa5u & (0x80u >> i)) != 0u ?
                           (uint8_t)(0x30u + i) : 0xaau;
        if (dst[i] != expected) return 0;
    }
    return 1;
}

static void set_draw_surface(uint32_t base, uint16_t pitch, uint32_t format,
                             int16_t width, int16_t height)
{
    ASTRAEA->DRAW_DST = base;
    ASTRAEA->DRAW_DST_PITCH = pitch;
    ASTRAEA->DRAW_FORMAT = format;
    ASTRAEA->DRAW_CLIP_MIN = DRAW_XY_(0, 0);
    ASTRAEA->DRAW_CLIP_MAX = DRAW_XY_(width, height);
    ASTRAEA->DRAW_WORK_ENTRIES = 0u;
}

static int draw_scene(void)
{
    static const uint8_t glyph_a[8] = {
        0x18u, 0x24u, 0x42u, 0x7eu, 0x42u, 0x42u, 0x42u, 0x00u
    };
    volatile uint8_t *glyph = ram8(GLYPH_MASK);
    volatile uint8_t *a4 = ram8(GLYPH_A4);
    uint32_t row;
    uint32_t column;

    set_draw_surface(FB_OFFSET, 720u, DRAW_FORMAT_INDEX8, 720, 480);

    ASTRAEA->DRAW_FG = 6u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(24, 24);
    ASTRAEA->DRAW_P1 = DRAW_XY_(695, 455);
    if (!draw(DRAW_OP_RECT)) return 0;

    ASTRAEA->DRAW_FG = 2u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(30, 40);
    ASTRAEA->DRAW_P1 = DRAW_XY_(690, 420);
    if (!draw(DRAW_OP_LINE)) return 0;
    ASTRAEA->DRAW_FG = 3u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(690, 40);
    ASTRAEA->DRAW_P1 = DRAW_XY_(30, 420);
    if (!draw(DRAW_OP_LINE)) return 0;

    ASTRAEA->DRAW_FG = 4u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(180, 150);
    ASTRAEA->DRAW_RADII = DRAW_RADII_(72, 72);
    if (!draw(DRAW_OP_CIRCLE_FILL)) return 0;
    ASTRAEA->DRAW_FG = 1u;
    if (!draw(DRAW_OP_CIRCLE)) return 0;

    ASTRAEA->DRAW_FG = 5u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(390, 150);
    ASTRAEA->DRAW_RADII = DRAW_RADII_(105, 48);
    if (!draw(DRAW_OP_ELLIPSE_FILL)) return 0;
    ASTRAEA->DRAW_FG = 1u;
    if (!draw(DRAW_OP_ELLIPSE)) return 0;

    ASTRAEA->DRAW_FG = 7u;
    ASTRAEA->DRAW_BG = 0u;
    ASTRAEA->DRAW_PATTERN_HI = 0xaa55aa55u;
    ASTRAEA->DRAW_PATTERN_LO = 0x55aa55aau;
    ASTRAEA->DRAW_ORIGIN = DRAW_XY_(0, 0);
    ASTRAEA->DRAW_P0 = DRAW_XY_(80, 285);
    ASTRAEA->DRAW_P1 = DRAW_XY_(300, 375);
    if (!draw(DRAW_OP_PATTERN_FILL)) return 0;

    ASTRAEA->DRAW_FG = 11u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(480, 285);
    ASTRAEA->DRAW_P1 = DRAW_XY_(660, 420);
    if (!draw(DRAW_OP_RECT)) return 0;
    ASTRAEA->DRAW_FG = 12u;
    ASTRAEA->DRAW_P0 = DRAW_XY_(500, 310);
    ASTRAEA->DRAW_WORK = FLOOD_WORK;
    ASTRAEA->DRAW_WORK_ENTRIES = 1024u;
    if (!draw(DRAW_OP_FLOOD_FILL)) return 0;
    ASTRAEA->DRAW_WORK_ENTRIES = 0u;

    for (row = 0; row < 8u; ++row)
        glyph[row] = glyph_a[row];
    ASTRAEA->DRAW_FG = 1u;
    ASTRAEA->DRAW_SRC = GLYPH_MASK;
    ASTRAEA->DRAW_SRC_PITCH = 1u;
    ASTRAEA->DRAW_SRC_SIZE = DRAW_SIZE_(8, 8);
    ASTRAEA->DRAW_P0 = DRAW_XY_(340, 400);
    ASTRAEA->DRAW_P1 = DRAW_XY_(0, 0);
    if (!draw(DRAW_OP_GLYPH_MASK1)) return 0;

    // Exercise RGB565 destination access and A4 blending off screen.
    for (row = 0; row < 8u; ++row) {
        for (column = 0; column < 4u; ++column)
            a4[row * 4u + column] = (uint8_t)(0x1fu + row * 0x11u);
    }
    if (!blit(0u, RGB_SCRATCH, 0u, 0u, 32u, 0u, 16u, 16u,
              BLIT_MODE_FILL | BLIT_ELEM16, 0x001fu, 0u))
        return 0;
    set_draw_surface(RGB_SCRATCH, 32u, DRAW_FORMAT_RGB565, 16, 16);
    ASTRAEA->DRAW_FG = 0xffffu;
    ASTRAEA->DRAW_SRC = GLYPH_A4;
    ASTRAEA->DRAW_SRC_PITCH = 4u;
    ASTRAEA->DRAW_SRC_SIZE = DRAW_SIZE_(8, 8);
    ASTRAEA->DRAW_P0 = DRAW_XY_(4, 4);
    ASTRAEA->DRAW_P1 = DRAW_XY_(0, 0);
    if (!draw(DRAW_OP_GLYPH_A4)) return 0;
    if (ram16(RGB_SCRATCH)[4u * 16u + 4u] == 0x001fu) return 0;

    return 1;
}

static void build_tiles(void)
{
    volatile uint16_t *map0 = ram16(TILE0_MAP);
    volatile uint16_t *map1 = ram16(TILE1_MAP);
    volatile uint8_t *set0 = ram8(TILE0_SET);
    volatile uint8_t *set1 = ram8(TILE1_SET);
    uint32_t x;
    uint32_t y;

    for (y = 0; y < 32u; ++y) {
        for (x = 0; x < 32u; ++x) {
            map0[y * 32u + x] = TILE_ENTRY((x + y) & 1u, 1u, 0u, 0u);
            map1[y * 32u + x] = TILE_ENTRY((x ^ y) & 1u, 2u, 0u, 0u);
        }
    }
    for (y = 0; y < 8u; ++y) {
        for (x = 0; x < 8u; x += 2u) {
            uint8_t p0 = ((x + y) & 7u) == 0u ? 1u : 0u;
            uint8_t p1 = ((x + 1u + y) & 7u) == 0u ? 1u : 0u;
            set0[y * 4u + x / 2u] = (uint8_t)((p0 << 4) | p1);
            set0[32u + y * 4u + x / 2u] =
                (uint8_t)((p1 << 4) | p0);
            p0 = (x == y || x + y == 7u) ? 1u : 0u;
            p1 = (x + 1u == y || x + 1u + y == 7u) ? 1u : 0u;
            set1[y * 4u + x / 2u] = (uint8_t)((p0 << 4) | p1);
            set1[32u + y * 4u + x / 2u] =
                (uint8_t)((p1 << 4) | p0);
        }
    }

    VEGA->TILE[0].MAP = TILE0_MAP;
    VEGA->TILE[0].SET = TILE0_SET;
    VEGA->TILE[0].SIZE = TILE_MAPSIZE(5, 5);
    VEGA->TILE[0].SCROLL = TILE_SCROLL(3, 5);
    VEGA->TILE[0].CTRL = DEMO_TILE0_ENABLE ?
        TILE_ENABLE | TILE_TRANSP_EN | TILE_WRAP : 0u;
    VEGA->TILE[1].MAP = TILE1_MAP;
    VEGA->TILE[1].SET = TILE1_SET;
    VEGA->TILE[1].SIZE = TILE_MAPSIZE(5, 5);
    VEGA->TILE[1].SCROLL = TILE_SCROLL(-2, 1);
    VEGA->TILE[1].CTRL = DEMO_TILE1_ENABLE ?
        TILE_ENABLE | TILE_TRANSP_EN | TILE_WRAP | TILE_ABOVE : 0u;
}

static void build_sprites(void)
{
#if DEMO_STRESS_SPRITES
    uint32_t sprite;
    uint32_t x;

    for (sprite = 0u; sprite < 32u; ++sprite) {
        volatile uint8_t *pattern =
            ram8(SPRITE_STRESS_SET + sprite * 0x1000u);
        for (x = 0u; x < 16u; ++x)
            pattern[x] = (uint8_t)(0x11u + ((sprite & 1u) * 0x11u));

        VEGA->SPR[sprite].CTRL =
            SPR_ENABLE | SPR_VISIBLE | SPR_COLLIDE_EN |
            (8u << SPR_PRIORITY_SHIFT) |
            (3u << SPR_PAL_BANK_SHIFT);
        VEGA->SPR[sprite].POS = VEGA_POS(0, 1);
        VEGA->SPR[sprite].SIZE = VEGA_SIZE(32, 1);
        VEGA->SPR[sprite].BASE = SPRITE_STRESS_SET + sprite * 0x1000u;
        VEGA->SPR[sprite].PITCH = 16u;
    }
    VEGA->SPR_BUDGET = DEMO_STRESS_BUDGET;
#else
    volatile uint8_t *pattern = ram8(SPRITE_SET);
    uint32_t x;
    uint32_t y;

    for (y = 0; y < 16u; ++y) {
        for (x = 0; x < 16u; x += 2u) {
            uint8_t p0 = (x > 1u && x < 14u && y > 1u && y < 14u) ?
                         (uint8_t)(1u + ((x + y) & 1u)) : 0u;
            uint8_t p1 = (x + 1u > 1u && x + 1u < 14u &&
                          y > 1u && y < 14u) ?
                         (uint8_t)(1u + ((x + 1u + y) & 1u)) : 0u;
            pattern[y * 8u + x / 2u] = (uint8_t)((p0 << 4) | p1);
        }
    }

    VEGA->SPR[0].CTRL = SPR_ENABLE | SPR_VISIBLE | SPR_COLLIDE_EN |
                        (8u << SPR_PRIORITY_SHIFT) |
                        (3u << SPR_PAL_BANK_SHIFT);
    VEGA->SPR[0].POS = VEGA_POS(330, 205);
    VEGA->SPR[0].SIZE = VEGA_SIZE(16, 16);
    VEGA->SPR[0].BASE = SPRITE_SET;
    VEGA->SPR[0].PITCH = 8u;
    VEGA->SPR[1].CTRL = SPR_ENABLE | SPR_VISIBLE | SPR_COLLIDE_EN |
                        SPR_FLIPX | (7u << SPR_PRIORITY_SHIFT) |
                        (3u << SPR_PAL_BANK_SHIFT);
    VEGA->SPR[1].POS = VEGA_POS(338, 212);
    VEGA->SPR[1].SIZE = VEGA_SIZE(16, 16);
    VEGA->SPR[1].BASE = SPRITE_SET;
    VEGA->SPR[1].PITCH = 8u;
    VEGA->SPR_BUDGET = 256u;
#endif
    VEGA->SPR_CTRL = VEGA_SPR_CTRL_ENABLE;
}

static void build_palette(void)
{
    VEGA->PAL[0] = VEGA_RGB(0, 0, 0);
    VEGA->PAL[1] = VEGA_RGB(255, 255, 255);
    VEGA->PAL[2] = VEGA_RGB(255, 70, 70);
    VEGA->PAL[3] = VEGA_RGB(70, 230, 120);
    VEGA->PAL[4] = VEGA_RGB(80, 130, 255);
    VEGA->PAL[5] = VEGA_RGB(255, 205, 60);
    VEGA->PAL[6] = VEGA_RGB(40, 225, 235);
    VEGA->PAL[7] = VEGA_RGB(230, 80, 220);
    VEGA->PAL[11] = VEGA_RGB(255, 255, 255);
    VEGA->PAL[12] = VEGA_RGB(40, 180, 130);
    VEGA->PAL[17] = VEGA_RGB(30, 70, 90);
    VEGA->PAL[18] = VEGA_RGB(50, 110, 130);
    VEGA->PAL[33] = VEGA_RGB(255, 255, 255);
    VEGA->PAL[34] = VEGA_RGB(150, 180, 200);
    VEGA->PAL[49] = VEGA_RGB(255, 150, 30);
    VEGA->PAL[50] = VEGA_RGB(255, 240, 80);
}

static void build_copper(void)
{
    static const uint32_t colors[8] = {
        0x000b1720u, 0x00132635u, 0x0019384au, 0x00214855u,
        0x002a4f58u, 0x003c4f58u, 0x00464650u, 0x00503b48u
    };
    uint32_t index = 0u;
    uint32_t band;
    for (band = 0u; band < 8u; ++band) {
        ASTRAEA->COP[index].w0 = COP_OP_WAIT | (band * 60u);
        ASTRAEA->COP[index].w1 = 0u;
        ++index;
        ASTRAEA->COP[index].w0 = COP_OP_MOVE | COP_OFF(&VEGA->BACKDROP);
        ASTRAEA->COP[index].w1 = colors[band];
        ++index;
    }
    ASTRAEA->COP[index].w0 = COP_OP_END;
    ASTRAEA->COP[index].w1 = 0u;
    ASTRAEA->COP_START = 0u;
    ASTRAEA->COP_CTRL = COP_ENABLE | COP_VBL_RESTART;
    ASTRAEA->COP_STROBE = 1u;
}

static int wait_frame(void)
{
    uint32_t timeout = TIMEOUT;
    while ((VEGA->STATUS & VEGA_STAT_VBLANK) != 0u && timeout != 0u)
        --timeout;
    while ((VEGA->STATUS & VEGA_STAT_VBLANK) == 0u && timeout != 0u)
        --timeout;
    return timeout != 0u;
}

void kmain(void)
{
    if (VESTA->ID != VESTA_ID_MAGIC || VEGA->ID != VEGA_ID_MAGIC ||
        ASTRAEA->ID != ASTRAEA_ID_MAGIC || !wait_sdram())
        fail(1u);
    if (VEGA->VERSION != 0x00030000u ||
        (VEGA->CAPS & 0x3fu) != 0x3fu ||
        ASTRAEA->VERSION != ASTRAEA_VERSION_0_3)
        fail(2u);

    set_leds(0x01u);
    if (!test_blitter()) fail(3u);
    if (!blit(0u, FB_OFFSET, 0u, 0u, 720u, 0u, 720u, 480u,
              BLIT_MODE_FILL | BLIT_ELEM8, 0u, 0u))
        fail(4u);
    if (!draw_scene()) fail(5u);

    build_palette();
    build_tiles();
    build_sprites();
    build_copper();

    VEGA->MODE = VEGA_MODE_720x480;
    VEGA->FB_BASE = FB_OFFSET;
#if DEMO_STRESS_RGB565
    VEGA->FB_PITCH = 1440u;
    VEGA->FB_FORMAT = VEGA_FMT_RGB565;
#else
    VEGA->FB_PITCH = 720u;
    VEGA->FB_FORMAT = VEGA_FMT_INDEX8;
#endif
    VEGA->FB_COLORKEY = 0u;
    VEGA->BACKDROP = 0x000b1720u;
    VEGA->IRQ_STAT = 0xffffffffu;
    VEGA->CTRL = VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_FB_EN |
                 VEGA_CTRL_SPR_EN | VEGA_CTRL_COLORKEY_EN |
                 VEGA_CTRL_BACKDROP_EN;

    if (!wait_frame() || !wait_frame()) fail(6u);
    VEGA->STATUS = VEGA_STAT_UNDERRUN;
    if (!wait_frame()) fail(7u);
    if (VEGA->STATUS & (VEGA_STAT_UNDERRUN | VEGA_STAT_CONFIG_ERROR))
        fail(8u);
    if ((VEGA->SPR_COLLISION & 3u) != 3u) fail(9u);
    if (ASTRAEA->COP_STATUS & (1u << 18)) fail(10u);

    set_leds(0x5au);
    for (;;) {
        if (VEGA->STATUS & (VEGA_STAT_UNDERRUN | VEGA_STAT_CONFIG_ERROR))
            fail(11u);
    }
}
