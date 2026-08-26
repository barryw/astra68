#include "splash.h"

#include "astraea.h"
#include "lz4_legacy.h"
#include "vega.h"

#include <stddef.h>
#include <stdint.h>

#define SDRAM_BASE 0x02000000u
#define SPLASH_WIDTH 720u
#define SPLASH_HEIGHT 480u
#define SPLASH_PITCH SPLASH_WIDTH
#define SPLASH_PIXEL_BYTES (SPLASH_WIDTH * SPLASH_HEIGHT)
#define SPLASH_PALETTE_BYTES (256u * 4u)
#define SPLASH_FONT_BYTES (256u * 16u)
#define SPLASH_ASSET_BYTES \
    (SPLASH_PIXEL_BYTES + SPLASH_PALETTE_BYTES + SPLASH_FONT_BYTES)

#define SPLASH_FB_A_ADDRESS ASTRA_BOOT_SPLASH_ADDRESS
#define SPLASH_FB_B_ADDRESS (ASTRA_BOOT_SPLASH_ADDRESS + 0x00058000u)
#define SPLASH_FB_A_OFFSET (SPLASH_FB_A_ADDRESS - SDRAM_BASE)
#define SPLASH_FB_B_OFFSET (SPLASH_FB_B_ADDRESS - SDRAM_BASE)
#define SPLASH_PALETTE_ADDRESS (SPLASH_FB_A_ADDRESS + SPLASH_PIXEL_BYTES)
#define SPLASH_FONT_ADDRESS \
    (SPLASH_PALETTE_ADDRESS + SPLASH_PALETTE_BYTES)
#define SPLASH_FONT_OFFSET (SPLASH_FONT_ADDRESS - SDRAM_BASE)
#define SPLASH_DESCRIPTORS_ADDRESS \
    (SPLASH_FONT_ADDRESS + SPLASH_FONT_BYTES)
#define SPLASH_DESCRIPTORS_OFFSET \
    (SPLASH_DESCRIPTORS_ADDRESS - SDRAM_BASE)

#define SPLASH_MAX_GLYPHS 64u
#define SPLASH_TIMEOUT_POLLS 10000000u
#define SPLASH_TEXT_X 84
#define SPLASH_MARK_X 66
#define SPLASH_STATE_X 520
#define SPLASH_FIRST_Y 344
#define SPLASH_ROW_STEP 22

#define SPLASH_COLOR_TEXT 252u
#define SPLASH_COLOR_ACCENT 253u
#define SPLASH_COLOR_OK 254u
#define SPLASH_COLOR_FAIL 255u

extern const uint8_t _splash_blob_start[];
extern const uint8_t _splash_blob_end[];

static const char *const status_labels[ASTRA_SPLASH_STATUS_COUNT] = {
    "Power-on self-test...",
    "Initializing graphics...",
    "Memory test: 32768 KB...",
    "Booting Axiom kernel..."
};

static uint32_t draw_fence = 1u;
static uint32_t blit_fence = 1u;
static uint32_t scene_generation = 1u;
static uint32_t front_buffer = SPLASH_FB_A_OFFSET;
static uint32_t back_buffer = SPLASH_FB_B_OFFSET;
static int splash_active;
static int scene_submitted;
static AstraSplashError splash_error;

static int wait_for_blitter(uint32_t fence)
{
    for (uint32_t polls = 0u; polls < SPLASH_TIMEOUT_POLLS; ++polls) {
        uint32_t status = ASTRAEA->BLIT_STATUS;

        if ((status & BLIT_BUSY) == 0u && (status & BLIT_DONE) != 0u)
            return BLIT_ERROR_CODE(status) == 0u &&
                   ASTRAEA->BLIT_FENCE == fence;
    }
    return 0;
}

static int copy_framebuffer(uint32_t source, uint32_t destination)
{
    uint32_t fence = blit_fence++;

    ASTRAEA->BLIT_SRC = source;
    ASTRAEA->BLIT_DST = destination;
    ASTRAEA->BLIT_MASK = 0u;
    ASTRAEA->BLIT_SRC_PITCH = SPLASH_PITCH;
    ASTRAEA->BLIT_DST_PITCH = SPLASH_PITCH;
    ASTRAEA->BLIT_MASK_PITCH = 0u;
    ASTRAEA->BLIT_DIM = BLIT_DIM_(SPLASH_WIDTH, SPLASH_HEIGHT);
    ASTRAEA->BLIT_OP = BLIT_MODE_COPY | BLIT_ELEM8;
    ASTRAEA->BLIT_COLOR = 0u;
    ASTRAEA->BLIT_KEY = 0u;
    ASTRAEA->BLIT_FENCE = fence;
    __asm__ volatile ("nop" ::: "memory");
    ASTRAEA->BLIT_CTRL = BLIT_START;
    return wait_for_blitter(fence);
}

static int wait_for_draw(uint32_t fence)
{
    for (uint32_t polls = 0u; polls < SPLASH_TIMEOUT_POLLS; ++polls) {
        uint32_t status = ASTRAEA->DRAW_STATUS;

        if ((status & DRAW_BUSY) == 0u && (status & DRAW_DONE) != 0u)
            return DRAW_ERROR_CODE(status) == 0u &&
                   ASTRAEA->DRAW_FENCE == fence;
    }
    return 0;
}

static int draw_text(uint32_t framebuffer, int16_t x, int16_t y,
                     uint8_t color, const char *text)
{
    volatile AstraeaGlyphDescriptor *descriptors =
        (volatile AstraeaGlyphDescriptor *)SPLASH_DESCRIPTORS_ADDRESS;
    uint32_t count = 0u;
    uint32_t fence;

    if (text == NULL)
        return 0;
    while (*text != '\0' && count < SPLASH_MAX_GLYPHS) {
        uint8_t glyph = (uint8_t)*text++;

        if (glyph < 0x20u || glyph == 0x7fu)
            glyph = (uint8_t)'?';
        descriptors[count].source_offset = (uint32_t)glyph * 16u;
        descriptors[count].source_position = DRAW_XY_(0u, 0u);
        descriptors[count].destination_position =
            DRAW_XY_(x + (int16_t)(count * 8u), y);
        descriptors[count].size = DRAW_SIZE_(8u, 16u);
        ++count;
    }
    if (*text != '\0' || count == 0u)
        return 0;

    fence = draw_fence++;
    ASTRAEA->DRAW_DST = framebuffer;
    ASTRAEA->DRAW_DST_PITCH = SPLASH_PITCH;
    ASTRAEA->DRAW_FORMAT = DRAW_FORMAT_INDEX8;
    ASTRAEA->DRAW_CLIP_MIN = DRAW_XY_(0, 0);
    ASTRAEA->DRAW_CLIP_MAX = DRAW_XY_(SPLASH_WIDTH, SPLASH_HEIGHT);
    ASTRAEA->DRAW_FG = color;
    ASTRAEA->DRAW_BG = 0u;
    ASTRAEA->DRAW_SRC = SPLASH_FONT_OFFSET;
    ASTRAEA->DRAW_SRC_PITCH = 1u;
    ASTRAEA->DRAW_SRC_SIZE = DRAW_SIZE_(8u, 16u);
    ASTRAEA->DRAW_WORK = SPLASH_DESCRIPTORS_OFFSET;
    ASTRAEA->DRAW_WORK_ENTRIES = count;
    ASTRAEA->DRAW_OP = DRAW_OP_GLYPH_MASK1;
    ASTRAEA->DRAW_FENCE = fence;
    __asm__ volatile ("nop" ::: "memory");
    ASTRAEA->DRAW_CTRL = DRAW_START;
    return wait_for_draw(fence);
}

static int wait_for_present(uint32_t generation)
{
    for (uint32_t polls = 0u; polls < SPLASH_TIMEOUT_POLLS; ++polls) {
        uint32_t status = VEGA->PRESENT_STATUS;

        if ((status & (VEGA_PRESENT_INVALID |
                       VEGA_PRESENT_COPY_DEADLINE)) != 0u)
            return 0;
        if (VEGA->PRESENT_COMPLETED_GENERATION == generation)
            return 1;
    }
    return 0;
}

static int present(uint32_t framebuffer, uint32_t control)
{
    uint32_t generation = scene_generation++;

    for (uint32_t polls = 0u; polls < SPLASH_TIMEOUT_POLLS; ++polls) {
        if ((VEGA->PRESENT_STATUS &
             (VEGA_PRESENT_PENDING | VEGA_PRESENT_COPY_BUSY)) == 0u)
            break;
        if (polls + 1u == SPLASH_TIMEOUT_POLLS)
            return 0;
    }
    VEGA->PRESENT_STATUS = VEGA_PRESENT_STICKY_MASK;
    VEGA->CTRL = control;
    VEGA->FB_BASE = framebuffer;
    VEGA->SCENE_GENERATION = generation;
    VEGA->DRAW_FENCE = draw_fence - 1u;
    VEGA->BLIT_FENCE = blit_fence - 1u;
    VEGA->PRESENT_CTRL = VEGA_PRESENT_SUBMIT;
    scene_submitted = 1;
    return wait_for_present(generation);
}

static void abandon_partial_start(void)
{
    AstraSplashError error = splash_error;

    if (scene_submitted)
        (void)astra_boot_splash_stop();
    splash_error = error;
}

static int draw_initial_labels(uint32_t framebuffer)
{
    for (uint32_t row = 0u; row < ASTRA_SPLASH_STATUS_COUNT; ++row) {
        int16_t y = (int16_t)(SPLASH_FIRST_Y + row * SPLASH_ROW_STEP);

        if (!draw_text(framebuffer, SPLASH_MARK_X, y,
                       SPLASH_COLOR_ACCENT, ">") ||
            !draw_text(framebuffer, SPLASH_TEXT_X, y,
                       SPLASH_COLOR_TEXT, status_labels[row]))
            return 0;
    }
    return 1;
}

static int update_status(AstraSplashStatus status, uint8_t color,
                         const char *text)
{
    uint32_t retired;
    int16_t y;

    if (!splash_active || status >= ASTRA_SPLASH_STATUS_COUNT) {
        splash_error = ASTRA_SPLASH_ERROR_STATUS_GLYPHS;
        return 0;
    }
    y = (int16_t)(SPLASH_FIRST_Y + (uint32_t)status * SPLASH_ROW_STEP);
    if (!draw_text(back_buffer, SPLASH_STATE_X, y, color, text)) {
        splash_error = ASTRA_SPLASH_ERROR_STATUS_GLYPHS;
        return 0;
    }
    retired = front_buffer;
    if (!present(back_buffer,
                 VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_FB_EN |
                 VEGA_CTRL_BACKDROP_EN)) {
        splash_error = ASTRA_SPLASH_ERROR_STATUS_PRESENT;
        return 0;
    }
    front_buffer = back_buffer;
    back_buffer = retired;

    // Keep both buffers identical so the next update needs only glyph work.
    if (!draw_text(back_buffer, SPLASH_STATE_X, y, color, text)) {
        splash_error = ASTRA_SPLASH_ERROR_STATUS_MIRROR;
        return 0;
    }
    splash_error = ASTRA_SPLASH_ERROR_NONE;
    return 1;
}

int astra_boot_splash_start(void)
{
    uint32_t compressed_size =
        (uint32_t)_splash_blob_end - (uint32_t)_splash_blob_start;
    uint8_t *asset = (uint8_t *)SPLASH_FB_A_ADDRESS;
    const uint8_t *palette = (const uint8_t *)SPLASH_PALETTE_ADDRESS;

    splash_active = 0;
    scene_submitted = 0;
    splash_error = ASTRA_SPLASH_ERROR_NONE;
    front_buffer = SPLASH_FB_A_OFFSET;
    back_buffer = SPLASH_FB_B_OFFSET;
    draw_fence = 1u;
    blit_fence = 1u;
    scene_generation = 1u;
    if (VEGA->ID != VEGA_ID_MAGIC ||
        (VEGA->CAPS & (VEGA_CAP_FRAMEBUFFER | VEGA_CAP_PALETTE |
                       VEGA_CAP_INDEX8)) !=
            (VEGA_CAP_FRAMEBUFFER | VEGA_CAP_PALETTE | VEGA_CAP_INDEX8) ||
        ASTRAEA->ID != ASTRAEA_ID_MAGIC ||
        (ASTRAEA->CAPS & (ASTRAEA_CAP_COPY | ASTRAEA_CAP_GLYPH)) !=
            (ASTRAEA_CAP_COPY | ASTRAEA_CAP_GLYPH)) {
        splash_error = ASTRA_SPLASH_ERROR_UNSUPPORTED;
        return 0;
    }
    if (astra_lz4_legacy_decode(
            _splash_blob_start, compressed_size, asset,
            SPLASH_ASSET_BYTES, SPLASH_ASSET_BYTES) != ASTRA_LZ4_OK) {
        splash_error = ASTRA_SPLASH_ERROR_DECODE;
        return 0;
    }
    for (uint32_t index = 0u; index < 256u; ++index) {
        uint32_t offset = index * 4u;
        VEGA->PAL[index] = VEGA_RGB(palette[offset + 2u],
                                    palette[offset + 1u],
                                    palette[offset + 0u]);
    }
    if (!copy_framebuffer(SPLASH_FB_A_OFFSET, SPLASH_FB_B_OFFSET)) {
        splash_error = ASTRA_SPLASH_ERROR_FRAME_COPY;
        return 0;
    }
    if (!draw_initial_labels(SPLASH_FB_A_OFFSET) ||
        !draw_initial_labels(SPLASH_FB_B_OFFSET)) {
        splash_error = ASTRA_SPLASH_ERROR_INITIAL_GLYPHS;
        return 0;
    }

    VEGA->MODE = VEGA_MODE_720x480;
    VEGA->FB_PITCH = SPLASH_PITCH;
    VEGA->FB_FORMAT = VEGA_FMT_INDEX8;
    VEGA->FB_COLORKEY = 0u;
    VEGA->FB_VIEW = VEGA_FB_VIEW_(0u, 0u);
    VEGA->FB_VIRTUAL = VEGA_FB_VIRTUAL_(SPLASH_WIDTH, SPLASH_HEIGHT);
    VEGA->FB_WRAP = 0u;
    VEGA->BACKDROP = VEGA_RGB(0u, 0u, 0u);
    VEGA->SPR_CTRL = 0u;
    VEGA->IRQ_STAT = 0xffffffffu;
    if (!present(SPLASH_FB_A_OFFSET,
                 VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_FB_EN |
                 VEGA_CTRL_BACKDROP_EN)) {
        splash_error = ASTRA_SPLASH_ERROR_INITIAL_PRESENT;
        abandon_partial_start();
        return 0;
    }
    splash_active = 1;
    return 1;
}

int astra_boot_splash_mark_ok(AstraSplashStatus status)
{
    return update_status(status, SPLASH_COLOR_OK, "OK");
}

int astra_boot_splash_mark_fail(AstraSplashStatus status)
{
    return update_status(status, SPLASH_COLOR_FAIL, "FAIL");
}

int astra_boot_splash_stop(void)
{
    if (!scene_submitted)
        return 1;
    if (!present(front_buffer, 0u)) {
        splash_error = ASTRA_SPLASH_ERROR_STOP_PRESENT;
        splash_active = 1;
        return 0;
    }
    splash_error = ASTRA_SPLASH_ERROR_NONE;
    splash_active = 0;
    scene_submitted = 0;
    return 1;
}

int astra_boot_splash_active(void)
{
    return splash_active;
}

AstraSplashError astra_boot_splash_error(void)
{
    return splash_error;
}

const char *astra_boot_splash_error_text(void)
{
    switch (splash_error) {
    case ASTRA_SPLASH_ERROR_NONE: return "none";
    case ASTRA_SPLASH_ERROR_UNSUPPORTED: return "unsupported hardware";
    case ASTRA_SPLASH_ERROR_DECODE: return "asset decode";
    case ASTRA_SPLASH_ERROR_FRAME_COPY: return "frame copy";
    case ASTRA_SPLASH_ERROR_INITIAL_GLYPHS: return "initial glyphs";
    case ASTRA_SPLASH_ERROR_INITIAL_PRESENT: return "initial present";
    case ASTRA_SPLASH_ERROR_STATUS_GLYPHS: return "status glyphs";
    case ASTRA_SPLASH_ERROR_STATUS_PRESENT: return "status present";
    case ASTRA_SPLASH_ERROR_STATUS_MIRROR: return "status mirror";
    case ASTRA_SPLASH_ERROR_STOP_PRESENT: return "stop present";
    default: return "invalid error";
    }
}
