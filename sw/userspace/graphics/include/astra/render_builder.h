#ifndef ASTRA_USERSPACE_RENDER_BUILDER_H
#define ASTRA_USERSPACE_RENDER_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include <astra/draw_list.h>

#define ASTRA_RENDER_BUILDER_BYTES 0x00040000u

typedef enum AstraRenderBuilderFailure {
    ASTRA_RENDER_BUILDER_FAILURE_NONE = 0u,
    ASTRA_RENDER_BUILDER_FAILURE_DATA,
    ASTRA_RENDER_BUILDER_FAILURE_DESCRIPTOR,
    ASTRA_RENDER_BUILDER_FAILURE_DESTINATION,
    ASTRA_RENDER_BUILDER_FAILURE_COMMAND,
    ASTRA_RENDER_BUILDER_FAILURE_SURFACE,
    ASTRA_RENDER_BUILDER_FAILURE_GLYPH,
} AstraRenderBuilderFailure;

typedef struct AstraRenderBuilder {
    uint8_t *bytes;
    uint32_t capacity;
    uint32_t generation;
    uint32_t command_count;
    uint32_t descriptor_count;
    uint32_t glyph_count;
    uint32_t data_cursor;
    uint32_t surface_cursor;
    uint32_t failed; /* AstraRenderBuilderFailure */
    uint16_t descriptor_width[128];
    uint16_t descriptor_height[128];
} AstraRenderBuilder;

int astra_render_builder_init(AstraRenderBuilder *builder, void *storage,
                              uint32_t bytes, uint32_t generation);
uint32_t astra_render_builder_frame(const AstraRenderBuilder *builder);
uint32_t astra_render_builder_surface(AstraRenderBuilder *builder,
                                      uint16_t width, uint16_t height);
uint32_t astra_render_builder_surface_at(AstraRenderBuilder *builder,
                                         uint32_t data_offset,
                                         uint16_t width, uint16_t height);
int astra_render_builder_fill(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t color);
int astra_render_builder_rounded(AstraRenderBuilder *builder,
                                 uint32_t destination, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 uint16_t radius, uint16_t color);
int astra_render_builder_text(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              const char *utf8, uint32_t length,
                              uint16_t pixel_height, uint16_t color);
int astra_render_builder_mono_text(AstraRenderBuilder *builder,
                                   uint32_t destination, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color);
int astra_draw_list_covers(const AstraDrawListHeader *header,
                           uint16_t width, uint16_t height);
int astra_render_builder_replay(AstraRenderBuilder *builder,
                                uint32_t destination,
                                const AstraDrawListHeader *draw_list);
int astra_render_builder_blit(AstraRenderBuilder *builder,
                              uint32_t destination, uint32_t source,
                              int32_t x, int32_t y, uint16_t width,
                              uint16_t height, uint16_t radius,
                              int round_top);
int astra_render_builder_blit_clipped(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t radius,
    int round_top, int32_t clip_left, int32_t clip_top,
    int32_t clip_right, int32_t clip_bottom);
int astra_render_builder_blit_region(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t source_x, int32_t source_y, int32_t destination_x,
    int32_t destination_y, uint16_t width, uint16_t height);
uint32_t astra_render_builder_finish(AstraRenderBuilder *builder);

#endif
